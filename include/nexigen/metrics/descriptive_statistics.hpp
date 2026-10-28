// nexigen::metrics — online descriptive statistics (design D9;
// spec metrics/descriptive-statistics).
//
// DescriptiveStatistics<Concurrency, Access, Reset> maintains count, min, max,
// mean and std in O(1) per sample with zero allocation (Welford's method).
// std is the POPULATION standard deviation.
//
// SingleThreaded — one plain moment cell; single-writer contract as the
// counter's (reads must not race recording without a happens-before edge).
//
// MultiThreaded — 32 exclusive per-thread slots plus a shared fallback cell.
// Each slot owns double-buffered banks (active/shadow) and a sequence
// counter, so a harvest swaps the window without blocking the recorder and
// without losing a concurrent record:
//
//   producer: seq <- odd (announce) | pick bank | update bank | seq <- even
//   harvester: swap active | wait for an even seq | read + clear the old bank
//
// A record that overlaps the swap is counted in exactly one window. Bank
// fields are relaxed ATOMICS (plain mov on x86-64): the sequence handshake
// provides the ordering, and the atomics keep the access TSan-clean.
//
// Threads beyond the 32-slot pool cannot use the single-writer protocol:
// their samples are dropped and counted in dropped_samples, so no loss is
// silent. Max_threads = 32 is the documented exactness capacity.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

#include <nexigen/metrics/concepts/metric_concept.hpp>
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/detail/probes.hpp>
#include <nexigen/metrics/detail/thread_slot.hpp>
#include <nexigen/metrics/detail/welford.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
class DescriptiveStatistics;

/// Construction guard: only a Meter may create instruments.
class MeterPasskey;

namespace detail {

/// One Welford bank: relaxed-atomic fields, single writer per slot.
struct alignas(64) Stats_bank {
    std::atomic<std::uint64_t> count{0};
    std::atomic<double> mean{0.0};
    std::atomic<double> m2{0.0};
    std::atomic<double> minimum{0.0};
    std::atomic<double> maximum{0.0};

    /// Read the bank into a plain moment state (harvester side).
    [[nodiscard]] Welford_state load_state() const noexcept {
        Welford_state state{};
        state.count = count.load(std::memory_order_relaxed);
        state.mean = mean.load(std::memory_order_relaxed);
        state.m2 = m2.load(std::memory_order_relaxed);
        state.min = minimum.load(std::memory_order_relaxed);
        state.max = maximum.load(std::memory_order_relaxed);
        return state;
    }

    /// Clear the bank (the harvester clears the bank it just took).
    void clear() noexcept {
        count.store(0, std::memory_order_relaxed);
        mean.store(0, std::memory_order_relaxed);
        m2.store(0, std::memory_order_relaxed);
        minimum.store(0, std::memory_order_relaxed);
        maximum.store(0, std::memory_order_relaxed);
    }

    /// Fold one sample in (producer side — single writer per slot).
    void apply(double value) noexcept {
        const std::uint64_t seen = count.load(std::memory_order_relaxed);
        if (seen == 0U) {
            count.store(1U, std::memory_order_relaxed);
            mean.store(value, std::memory_order_relaxed);
            m2.store(0.0, std::memory_order_relaxed);
            minimum.store(value, std::memory_order_relaxed);
            maximum.store(value, std::memory_order_relaxed);
            return;
        }
        const double current_mean = mean.load(std::memory_order_relaxed);
        const double delta = value - current_mean;
        const double next_mean = current_mean + delta / static_cast<double>(seen + 1U);
        const double delta2 = value - next_mean;
        const double current_m2 = m2.load(std::memory_order_relaxed);
        count.store(seen + 1U, std::memory_order_relaxed);
        mean.store(next_mean, std::memory_order_relaxed);
        m2.store(current_m2 + delta * delta2, std::memory_order_relaxed);
        if (value < minimum.load(std::memory_order_relaxed)) {
            minimum.store(value, std::memory_order_relaxed);
        }
        if (value > maximum.load(std::memory_order_relaxed)) {
            maximum.store(value, std::memory_order_relaxed);
        }
    }
};

/// A recording slot: double-buffered banks plus the sequence handshake.
struct alignas(64) Stats_slot {
    Stats_bank bank[2];
    std::atomic<std::uint32_t> seq{0};
    std::atomic<std::uint8_t> active{0};
};

/// Maximum number of handshake attempts before a slot is deferred to the next
/// window (deferring loses nothing: the samples stay in the bank).
inline constexpr int stats_handshake_attempts = 64;

} // namespace detail

/// Online moment statistics. Constructible only through a Meter.
template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
          AccessPolicy Access = AccessPolicy::Readable,
          ResetPolicy Reset = ResetPolicy::Cumulative>
class DescriptiveStatistics {
public:
    static constexpr MetricKind kind = MetricKind::DescriptiveStatistics;
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    static constexpr AccessPolicy access = Access;
    static constexpr ResetPolicy reset = Reset;

    DescriptiveStatistics(const DescriptiveStatistics&) = delete;
    DescriptiveStatistics& operator=(const DescriptiveStatistics&) = delete;
    DescriptiveStatistics(DescriptiveStatistics&&) = delete;
    DescriptiveStatistics& operator=(DescriptiveStatistics&&) = delete;

    /// Record one sample. Non-finite samples are ignored (and counted).
    void record(double value) noexcept {
        if (!std::isfinite(value)) {
            ignored_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            detail::welford_add(cell_, value);
        } else {
            const std::uint8_t slot = detail::thread_slot(MetricKind::DescriptiveStatistics);
            if (slot == detail::fallback_slot) {
                dropped_.fetch_add(1U, std::memory_order_relaxed);
                return;
            }
            auto& target = slots_[slot];
            const std::uint32_t marker = target.seq.load(std::memory_order_relaxed);
            // Announce as a full barrier (xchg on x86): the update below must
            // not become visible before the announcement, or the harvester's
            // even-seq check can pass mid-record and tear the window (design
            // D9/D15).
            target.seq.store(marker + 1U, std::memory_order_seq_cst);
            const std::uint8_t bank = target.active.load(std::memory_order_relaxed);
            target.bank[bank].apply(value);
            target.seq.store(marker + 2U, std::memory_order_release);   // done
        }
    }

    /// Current summary (or the window summary for ResetOnRead — which also
    /// clears the window). Available only for Readable instruments.
    [[nodiscard]] StatisticsSnapshot read() const requires (Access == AccessPolicy::Readable) {
        StatisticsSnapshot snapshot{};
        snapshot.ignored_samples = ignored_.load(std::memory_order_relaxed);
        snapshot.dropped_samples = dropped_.load(std::memory_order_relaxed);
        const detail::Welford_state state = take_state();
        snapshot.count = state.count;
        snapshot.min = state.min;
        snapshot.max = state.max;
        snapshot.mean = state.mean;
        snapshot.stddev = detail::population_std(state);
        return snapshot;
    }

    /// Full record for this instrument (identity, classification, values).
    [[nodiscard]] MetricRecord snapshot() const requires (Access == AccessPolicy::Readable) {
        MetricRecord record{};
        record.kind = kind;
        record.concurrency = concurrency;
        record.access = access;
        record.reset = reset;
        record.statistics = read();
        if (identity_ != nullptr) {
            record.identity = *identity_;
        }
        return record;
    }

    /// Cold-path diagnostic (mandate 9). Never resets a window.
    [[nodiscard]] std::string to_string() const {
        const detail::Welford_state state = observe_state();
        std::string out = "DescriptiveStatistics(kind=";
        out += std::string(metrics::to_string(kind));
        out += ", concurrency=" + std::string(metrics::to_string(concurrency));
        out += ", access=" + std::string(metrics::to_string(access));
        out += ", reset=" + std::string(metrics::to_string(reset));
        out += ", count=" + std::to_string(state.count);
        out += ", min=" + std::to_string(state.min);
        out += ", max=" + std::to_string(state.max);
        out += ", mean=" + std::to_string(state.mean);
        out += ", std=" + std::to_string(detail::population_std(state));
        out += ")";
        return out;
    }

    /// Diagnostic JSON (cold path). The instrument is NOT deserializable: its
    /// construction is Meter-only.
    friend void to_json(nlohmann::json& j, const DescriptiveStatistics& statistics) {
        const StatisticsSnapshot snapshot = statistics.read();
        j = nlohmann::json{
            {"kind", std::string(metrics::to_string(DescriptiveStatistics::kind))},
            {"concurrency", std::string(metrics::to_string(DescriptiveStatistics::concurrency))},
            {"access", std::string(metrics::to_string(DescriptiveStatistics::access))},
            {"reset", std::string(metrics::to_string(DescriptiveStatistics::reset))},
            {"statistics", snapshot},
        };
    }

private:
    friend class Meter;
    friend class MetricProvider;

    explicit DescriptiveStatistics(const MeterPasskey&) noexcept {}

    static void harvest_thunk(void* object, MetricRecord& record) noexcept {
        const auto& self = *static_cast<const DescriptiveStatistics*>(object);
        const detail::Welford_state state = self.take_state();
        record.statistics.ignored_samples = self.ignored_.load(std::memory_order_relaxed);
        record.statistics.dropped_samples = self.dropped_.load(std::memory_order_relaxed);
        record.statistics.count = state.count;
        record.statistics.min = state.min;
        record.statistics.max = state.max;
        record.statistics.mean = state.mean;
        record.statistics.stddev = detail::population_std(state);
    }

    static void destroy_thunk(void* object) noexcept {
        static_cast<DescriptiveStatistics*>(object)->~DescriptiveStatistics();
    }

    /// Non-destructive summary (never clears a window).
    [[nodiscard]] detail::Welford_state observe_state() const noexcept {
        detail::Welford_state total{};
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            return cell_;
        } else {
            for (std::size_t i = 0; i < detail::shard_count; ++i) {
                total = detail::welford_merge(total, peek_slot(i));
            }
            return total;
        }
    }

    /// Window-consuming summary: ResetOnRead clears what it reports.
    [[nodiscard]] detail::Welford_state take_state() const noexcept {
        detail::Welford_state total{};
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            if constexpr (Reset == ResetPolicy::ResetOnRead) {
                const detail::Welford_state state = cell_;
                cell_ = detail::Welford_state{};
                return state;
            } else {
                return cell_;
            }
        } else {
            for (std::size_t i = 0; i < detail::shard_count; ++i) {
                total = detail::welford_merge(total, take_slot(i));
            }
            return total;
        }
    }

    /// Read one slot without touching its window.
    [[nodiscard]] detail::Welford_state peek_slot(std::size_t index) const noexcept {
        auto& slot = slots_[index];
        for (int attempt = 0; attempt < detail::stats_handshake_attempts; ++attempt) {
            const std::uint32_t before = slot.seq.load(std::memory_order_acquire);
            if ((before & 1U) != 0U) {
                continue; // a producer is mid-record
            }
            const std::uint8_t bank = slot.active.load(std::memory_order_relaxed);
            const detail::Welford_state state = slot.bank[bank].load_state();
            // Keep the bank reads inside the checked window: compiler barrier
            // only (x86 TSO already orders loads; design D9).
            std::atomic_signal_fence(std::memory_order_acq_rel);
            if (slot.seq.load(std::memory_order_acquire) == before) {
                return state;
            }
        }
        return detail::Welford_state{}; // deferred to the next window
    }

    /// Read one slot; ResetOnRead swaps the active bank and clears it.
    [[nodiscard]] detail::Welford_state take_slot(std::size_t index) const noexcept {
        auto& slot = slots_[index];
        if constexpr (Reset == ResetPolicy::ResetOnRead) {
            const std::uint8_t current = slot.active.load(std::memory_order_relaxed);
            const std::uint8_t taken = slot.active.exchange(static_cast<std::uint8_t>(current ^ 1U),
                                                            std::memory_order_acq_rel);
            // Read and clear the taken bank only under a STABLE even sequence:
            // an odd one means a producer is mid-record, and clearing under it
            // would tear the multi-step apply (already-counted samples could be
            // resurrected, the in-flight one lost). The before/after re-check
            // proves the load observed no producer activity at all.
            for (int attempt = 0; attempt < detail::stats_handshake_attempts; ++attempt) {
                const std::uint32_t before = slot.seq.load(std::memory_order_acquire);
                if ((before & 1U) != 0U) {
                    continue;
                }
                const detail::Welford_state state = slot.bank[taken].load_state();
                // Keep the bank reads inside the checked window: compiler
                // barrier only (x86 TSO already orders loads; design D9).
                std::atomic_signal_fence(std::memory_order_acq_rel);
                if (slot.seq.load(std::memory_order_acquire) == before) {
                    slot.bank[taken].clear();
                    return state;
                }
            }
            // Deferred to a later window: the bank is left untouched, so the
            // racing record completes into it and is read exactly once at the
            // swap that takes this bank again (no loss, no double count).
            return detail::Welford_state{};
        } else {
            return peek_slot(index);
        }
    }

    /// Identity of the owning registry slot (set by the Meter at creation).
    const MetricIdentity* identity_{nullptr};

    mutable detail::Welford_state cell_{};                  // SingleThreaded
    mutable detail::Stats_slot slots_[detail::shard_count]{}; // MultiThreaded
    std::atomic<std::uint64_t> ignored_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

static_assert(sizeof(DescriptiveStatistics<ConcurrencyPolicy::MultiThreaded, AccessPolicy::Readable,
                                            ResetPolicy::ResetOnRead>) <=
                  detail::max_statistics_size,
              "statistics must fit the registry budget");
static_assert(concepts::Metric<DescriptiveStatistics<ConcurrencyPolicy::SingleThreaded,
                                                      AccessPolicy::Readable,
                                                      ResetPolicy::Cumulative>>,
              "DescriptiveStatistics must satisfy the Metric concept");
static_assert(concepts::ReadableMetric<DescriptiveStatistics<ConcurrencyPolicy::SingleThreaded,
                                                              AccessPolicy::Readable,
                                                              ResetPolicy::Cumulative>>,
              "a Readable statistics instrument must satisfy the ReadableMetric concept");
static_assert(!concepts::ReadableMetric<DescriptiveStatistics<ConcurrencyPolicy::SingleThreaded,
                                                               AccessPolicy::WriteOnly,
                                                               ResetPolicy::Cumulative>>,
              "a WriteOnly statistics instrument must NOT satisfy the ReadableMetric concept");
static_assert(!detail::HasRead<DescriptiveStatistics<ConcurrencyPolicy::SingleThreaded,
                                                       AccessPolicy::WriteOnly,
                                                       ResetPolicy::Cumulative>>,
              "a WriteOnly statistics instrument must not expose read()");
static_assert(!detail::HasSnapshot<DescriptiveStatistics<ConcurrencyPolicy::SingleThreaded,
                                                          AccessPolicy::WriteOnly,
                                                          ResetPolicy::Cumulative>>,
              "a WriteOnly statistics instrument must not expose snapshot()");

/// Free helper (mandate 9): mirrors the member.
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
[[nodiscard]] std::string to_string(
    const nexigen::metrics::DescriptiveStatistics<Concurrency, Access, Reset>& statistics) {
    return statistics.to_string();
}

} // namespace nexigen::metrics


/// Statistics instruments format as their diagnostic string (mandate 11).
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::DescriptiveStatistics<Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::DescriptiveStatistics<Concurrency, Access, Reset>& statistics,
                Ctx& ctx) const {
        const std::string text = statistics.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
