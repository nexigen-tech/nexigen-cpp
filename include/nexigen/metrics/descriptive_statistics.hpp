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
// silent. MaxThreads = 32 is the documented exactness capacity.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

#include <nexigen/metrics/concepts/metric_concept.hpp>
#include <nexigen/metrics/detail/instrument_helpers.hpp>
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/detail/probes.hpp>
#include <nexigen/metrics/detail/thread_slot.hpp>
#include <nexigen/metrics/detail/welford.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

/// @brief Online moment statistics, forward-declared before its bank helpers.
///
/// The definition below carries the full contract; the declaration lets
/// `detail::StatsBank` and the `Meter` friend name the type.
///
/// @tparam Concurrency Concurrency policy: `SingleThreaded` or `MultiThreaded`.
/// @tparam Access Access policy: `Readable` or `WriteOnly`.
/// @tparam Reset Reset policy: `Cumulative` or `ResetOnRead`.
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
class DescriptiveStatistics;

/// @brief Construction guard proving that only a `Meter` creates instruments.
///
/// The type stays incomplete in this header: instruments take it by reference
/// in a private constructor, so application code cannot fabricate one and
/// every instrument remains registry-owned for its whole lifetime.
class MeterPasskey;

namespace detail {

/// @brief One Welford bank: relaxed-atomic fields with a single writer per slot.
///
/// The double-buffered pair of banks plus the slot sequence is what makes a
/// reset window non-blocking: the producer keeps updating the active bank
/// while the harvester drains the other one. Fields are atomic so the access
/// pattern is TSan-clean; the seqlock sequence supplies the ordering.
struct alignas(64) StatsBank {
    std::atomic<std::uint64_t> count{0}; ///< Number of samples folded into the bank.
    std::atomic<double> mean{0.0};       ///< Running mean of the samples.
    std::atomic<double> m2{0.0};         ///< Sum of squared deviations from the mean.
    std::atomic<double> minimum{0.0};    ///< Smallest sample seen in this bank.
    std::atomic<double> maximum{0.0};    ///< Largest sample seen in this bank.

    /// @brief Read the bank into a plain moment state.
    ///
    /// Runs on the harvester side inside the checked sequence window.
    ///
    /// @return Snapshot of `count`, `mean`, `m2`, `min` and `max`.
    [[nodiscard]] WelfordState load_state() const noexcept {
        WelfordState state{};
        state.count = count.load(std::memory_order_relaxed);
        state.mean = mean.load(std::memory_order_relaxed);
        state.m2 = m2.load(std::memory_order_relaxed);
        state.min = minimum.load(std::memory_order_relaxed);
        state.max = maximum.load(std::memory_order_relaxed);
        return state;
    }

    /// @brief Clear every field of the bank.
    ///
    /// Used by the harvester on the bank it has just taken.
    void clear() noexcept {
        count.store(0, std::memory_order_relaxed);
        mean.store(0, std::memory_order_relaxed);
        m2.store(0, std::memory_order_relaxed);
        minimum.store(0, std::memory_order_relaxed);
        maximum.store(0, std::memory_order_relaxed);
    }

    /// @brief Fold one sample into the bank with Welford's update.
    ///
    /// The first sample seeds `count`, `mean`, `min` and `max`; later samples
    /// update the mean and `m2` incrementally, keeping the cost O(1) and the
    /// numerics stable.
    ///
    /// @param value Finite sample to fold in; non-finite values are filtered
    ///        out by the instrument before reaching the bank.
    /// @warning Single writer per slot: the recording thread owns the bank.
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

/// @brief A recording slot: the double-buffered banks plus the sequence handshake.
///
/// `seq` is even while the slot is stable and odd while a producer is
/// mid-record; `active` selects the bank the producer writes. A harvest swaps
/// `active` and drains the bank it just took, so the swap never blocks a
/// recording thread.
struct alignas(64) StatsSlot {
    StatsBank bank[2];                 ///< Active/shadow banks; `active` selects the recording one.
    std::atomic<std::uint32_t> seq{0}; ///< Handshake sequence: odd while a record is in flight.
    std::atomic<std::uint8_t> active{0}; ///< Index of the bank the producer records into.
};

/// @brief Maximum handshake retries before a slot is deferred to the next window.
///
/// Deferring loses nothing: the samples stay in the bank and are reported by
/// the following window. The bound keeps the harvest path predictable instead
/// of spinning indefinitely under a busy producer.
inline constexpr int stats_handshake_attempts = 64;

} // namespace detail

/// @brief Online `count`/`min`/`max`/`mean`/`std` instrument with zero allocation.
///
/// Maintains the moment summary in O(1) per sample with Welford's numerically
/// stable update, keeping `std` as the population standard deviation. The
/// storage strategy is chosen at compile time: `SingleThreaded` keeps one
/// plain moment cell, while `MultiThreaded` keeps 32 cache-line-aligned
/// per-thread slots with double-buffered banks plus a shared fallback, so a
/// harvest swaps the window without blocking recording. Construction is
/// `Meter`-only; the type is neither copyable nor movable, and the registry
/// owns its storage for the instrument lifetime.
///
/// `record()` is the recording surface; `read()`, `snapshot()`, harvesting and
/// `to_string()` belong to the cold path. Samples from threads beyond the
/// 32-slot pool are dropped and counted, so no loss is silent.
///
/// @tparam Concurrency Concurrency policy, `ConcurrencyPolicy::SingleThreaded`
///         or `ConcurrencyPolicy::MultiThreaded`.
/// @tparam Access Access policy. `Readable` exposes `read()`/`snapshot()`;
///         `WriteOnly` removes them at compile time.
/// @tparam Reset Reset policy. `Cumulative` accumulates over the instrument
///         lifetime; `ResetOnRead` reports and clears the window.
/// @note Spec `metrics/descriptive-statistics`, requirement "Online moment
///       statistics".
/// @note Satisfies `concepts::Metric` and, for
///       `Access == AccessPolicy::Readable` only,
///       `concepts::ReadableMetric` (spec `metrics/metric-concepts`,
///       requirement "Readable metric concept").
/// @warning Under `ConcurrencyPolicy::SingleThreaded` there is one writer:
///          reads and harvests must not run concurrently with recording
///          unless the caller establishes happens-before (for example with
///          `collect_now()` on the recording thread).
template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
          AccessPolicy Access = AccessPolicy::Readable, ResetPolicy Reset = ResetPolicy::Cumulative>
class DescriptiveStatistics {
public:
    /// @brief Metric kind discriminant reported in every snapshot.
    static constexpr MetricKind kind = MetricKind::DescriptiveStatistics;
    /// @brief Concurrency policy this instantiation was built with.
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    /// @brief Access policy controlling whether the read side is exposed.
    static constexpr AccessPolicy access = Access;
    /// @brief Reset policy controlling cumulative versus windowed reporting.
    static constexpr ResetPolicy reset = Reset;

    /// @brief Copy construction is deleted: instruments are registry-owned.
    DescriptiveStatistics(const DescriptiveStatistics&) = delete;
    /// @brief Copy assignment is deleted: instruments are registry-owned.
    DescriptiveStatistics& operator=(const DescriptiveStatistics&) = delete;
    /// @brief Move construction is deleted: instrument addresses are stable.
    DescriptiveStatistics(DescriptiveStatistics&&) = delete;
    /// @brief Move assignment is deleted: instrument addresses are stable.
    DescriptiveStatistics& operator=(DescriptiveStatistics&&) = delete;

    /// @brief Record one sample.
    ///
    /// A non-finite sample (NaN or either infinity) leaves the moments
    /// untouched and increments the visible ignored-sample counter. A finite
    /// sample lands in exactly one reset window; under `MultiThreaded` it goes
    /// to the caller's own slot with no cross-core contention.
    ///
    /// @param value Finite sample to fold into the moments; NaN and infinities
    ///        are ignored and counted rather than folded.
    /// @note Spec `metrics/descriptive-statistics`, requirement "Online moment
    ///       statistics".
    /// @note Spec `metrics/descriptive-statistics`, requirement "Multi-threaded
    ///       statistics merging".
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
            target.seq.store(marker + 2U, std::memory_order_release); // done
        }
    }

    /// @brief Current moment summary, or the window summary under `ResetOnRead`.
    ///
    /// Under `ResetPolicy::ResetOnRead` the call consumes the window: every
    /// slot is drained (the MultiThreaded path swaps banks) so the samples
    /// reported once are not reported again. `stddev` is the population
    /// standard deviation, `0.0` for one sample and for an empty window.
    ///
    /// @return Snapshot with `count`, `min`, `max`, `mean`, `stddev`,
    ///         `ignored_samples` and `dropped_samples` filled in.
    /// @note Spec `metrics/descriptive-statistics`, requirement "Readable
    ///       statistics surface".
    /// @note Spec `metrics/descriptive-statistics`, requirement "Reset-on-read
    ///       statistics windows".
    /// @note Available only when `Access == AccessPolicy::Readable` (spec
    ///       `metrics/metric-concepts`, requirement "Compile-time access
    ///       gating").
    [[nodiscard]] StatisticsSnapshot read() const
        requires(Access == AccessPolicy::Readable)
    {
        StatisticsSnapshot snapshot{};
        snapshot.ignored_samples = ignored_.load(std::memory_order_relaxed);
        snapshot.dropped_samples = dropped_.load(std::memory_order_relaxed);
        const detail::WelfordState state = take_state();
        snapshot.count = state.count;
        snapshot.min = state.min;
        snapshot.max = state.max;
        snapshot.mean = state.mean;
        snapshot.stddev = detail::population_std(state);
        return snapshot;
    }

    /// @brief Build the full `MetricRecord` for this instrument.
    ///
    /// Carries `MetricKind::DescriptiveStatistics`, the meter and metric
    /// identity, the policy classification and the current summary. Under
    /// `ResetOnRead` it consumes the window exactly like `read()`.
    ///
    /// @return Record with classification filled in and `statistics` set.
    /// @note Spec `metrics/descriptive-statistics`, requirement "Readable
    ///       statistics surface".
    /// @note Available only when `Access == AccessPolicy::Readable` (spec
    ///       `metrics/metric-concepts`, requirement "Compile-time access
    ///       gating").
    [[nodiscard]] MetricRecord snapshot() const
        requires(Access == AccessPolicy::Readable)
    {
        MetricRecord record = detail::make_record(kind, concurrency, access, reset, identity_);
        record.statistics = read();
        return record;
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// The text carries the kind, the policy classification and the current
    /// moments. It never resets a `ResetOnRead` window.
    ///
    /// @return Diagnostic text; the free `to_string()` overload returns the
    ///         same string.
    /// @note Project mandate 9 (member and free `to_string`).
    /// @note Spec `metrics/descriptive-statistics`, requirement "Statistics
    ///       serialization contract".
    [[nodiscard]] std::string to_string() const {
        const detail::WelfordState state = observe_state();
        std::string out = "DescriptiveStatistics(kind=";
        detail::append_classification(out, kind, concurrency, access, reset);
        out += ", count=" + std::to_string(state.count);
        out += ", min=" + std::to_string(state.min);
        out += ", max=" + std::to_string(state.max);
        out += ", mean=" + std::to_string(state.mean);
        out += ", std=" + std::to_string(detail::population_std(state));
        out += ")";
        return out;
    }

    /// @brief Serialize the statistics instrument as diagnostic JSON.
    ///
    /// The object carries the classification fields plus a `"statistics"`
    /// object with the current summary. It is deliberately write-only: there
    /// is no `from_json`, because an instrument can only be created by a
    /// `Meter`. Note that this uses the consuming `read()`, so under
    /// `ResetOnRead` serializing also clears the window.
    ///
    /// @param j JSON value to assign to.
    /// @param statistics Instrument being serialized.
    /// @note Project mandate 10 (non-throwing `to_json` under
    ///       `JSON_NOEXCEPTION=1`).
    /// @note Spec `metrics/descriptive-statistics`, requirement "Statistics
    ///       serialization contract".
    friend void to_json(nlohmann::json& j, const DescriptiveStatistics& statistics) {
        const StatisticsSnapshot snapshot = statistics.read();
        j = detail::classification_json(
            DescriptiveStatistics::kind, DescriptiveStatistics::concurrency,
            DescriptiveStatistics::access, DescriptiveStatistics::reset);
        j["statistics"] = snapshot;
    }

private:
    friend class Meter;
    friend class MetricProvider;

    /// @brief Create empty statistics; the unnamed passkey proves `Meter` origin.
    ///
    /// The argument is intentionally unnamed: it only has to exist, and it can
    /// only be produced by `Meter`, so no other code path can construct an
    /// instrument.
    explicit DescriptiveStatistics(const MeterPasskey&) noexcept {}

    /// @brief Type-erased harvest hook used by the provider's plain C function pointer.
    ///
    /// @param object Instrument addressed as `void*` by the registry slot.
    /// @param record Record whose `statistics` payload receives the window.
    /// @note Spec `metrics/descriptive-statistics`, requirement "Reset-on-read
    ///       statistics windows".
    static void harvest_thunk(void* object, MetricRecord& record) noexcept {
        const auto& self = *static_cast<const DescriptiveStatistics*>(object);
        const detail::WelfordState state = self.take_state();
        record.statistics.ignored_samples = self.ignored_.load(std::memory_order_relaxed);
        record.statistics.dropped_samples = self.dropped_.load(std::memory_order_relaxed);
        record.statistics.count = state.count;
        record.statistics.min = state.min;
        record.statistics.max = state.max;
        record.statistics.mean = state.mean;
        record.statistics.stddev = detail::population_std(state);
    }

    /// @brief Non-destructive summary that never clears a window.
    ///
    /// @return Merged moments over every slot, or the single cell.
    [[nodiscard]] detail::WelfordState observe_state() const noexcept {
        detail::WelfordState total{};
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            return cell_;
        } else {
            for (std::size_t i = 0; i < detail::shard_count; ++i) {
                total = detail::welford_merge(total, peek_slot(i));
            }
            return total;
        }
    }

    /// @brief Window-consuming summary: under `ResetOnRead` it clears what it reports.
    ///
    /// @return Merged moments over every slot, or the single cell, with the
    ///         reported window cleared when `Reset` is
    ///         `ResetPolicy::ResetOnRead`.
    /// @note Spec `metrics/descriptive-statistics`, requirement "Reset-on-read
    ///       statistics windows".
    [[nodiscard]] detail::WelfordState take_state() const noexcept {
        detail::WelfordState total{};
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            if constexpr (Reset == ResetPolicy::ResetOnRead) {
                const detail::WelfordState state = cell_;
                cell_ = detail::WelfordState{};
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

    /// @brief Read one slot without touching its window.
    ///
    /// Spins at most `detail::stats_handshake_attempts` times waiting for a
    /// stable even sequence.
    ///
    /// @param index Slot index below `detail::shard_count`.
    /// @return Moments observed during a stable window, or an empty state when
    ///         the bounded handshake gives up (the samples are deferred).
    [[nodiscard]] detail::WelfordState peek_slot(std::size_t index) const noexcept {
        auto& slot = slots_[index];
        for (int attempt = 0; attempt < detail::stats_handshake_attempts; ++attempt) {
            const std::uint32_t before = slot.seq.load(std::memory_order_acquire);
            if ((before & 1U) != 0U) {
                continue; // a producer is mid-record
            }
            const std::uint8_t bank = slot.active.load(std::memory_order_relaxed);
            const detail::WelfordState state = slot.bank[bank].load_state();
            // Keep the bank reads inside the checked window: compiler barrier
            // only (x86 TSO already orders loads; design D9).
            std::atomic_signal_fence(std::memory_order_acq_rel);
            if (slot.seq.load(std::memory_order_acquire) == before) {
                return state;
            }
        }
        return detail::WelfordState{}; // deferred to the next window
    }

    /// @brief Read one slot; under `ResetOnRead` swap the active bank and clear it.
    ///
    /// The swap happens before the handshake, so a concurrent record lands in
    /// the other bank and is read exactly once by the window that takes this
    /// bank again. When the bounded handshake cannot observe a stable even
    /// sequence the taken bank is left untouched and the samples are deferred,
    /// never lost or double counted.
    ///
    /// @param index Slot index below `detail::shard_count`.
    /// @return Moments of the taken window, or an empty state when deferred.
    /// @note Spec `metrics/descriptive-statistics`, requirement "Reset-on-read
    ///       statistics windows".
    [[nodiscard]] detail::WelfordState take_slot(std::size_t index) const noexcept {
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
                const detail::WelfordState state = slot.bank[taken].load_state();
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
            return detail::WelfordState{};
        } else {
            return peek_slot(index);
        }
    }

    /// @brief Identity of the owning registry slot, set by the `Meter` at creation.
    const MetricIdentity* identity_{nullptr};

    /// @brief Single-threaded moment cell; unused under `MultiThreaded`.
    mutable detail::WelfordState cell_{};
    /// @brief Per-thread slots plus the shared fallback used under `MultiThreaded`.
    mutable detail::StatsSlot slots_[detail::shard_count]{};
    /// @brief Samples rejected because they were NaN or infinite.
    std::atomic<std::uint64_t> ignored_{0};
    /// @brief Samples dropped because the recording thread had no exclusive slot.
    std::atomic<std::uint64_t> dropped_{0};
};

static_assert(sizeof(DescriptiveStatistics<ConcurrencyPolicy::MultiThreaded, AccessPolicy::Readable,
                                           ResetPolicy::ResetOnRead>) <=
                  detail::max_statistics_size,
              "statistics must fit the registry budget");
static_assert(
    concepts::Metric<DescriptiveStatistics<ConcurrencyPolicy::SingleThreaded,
                                           AccessPolicy::Readable, ResetPolicy::Cumulative>>,
    "DescriptiveStatistics must satisfy the Metric concept");
static_assert(
    concepts::ReadableMetric<DescriptiveStatistics<
        ConcurrencyPolicy::SingleThreaded, AccessPolicy::Readable, ResetPolicy::Cumulative>>,
    "a Readable statistics instrument must satisfy the ReadableMetric concept");
static_assert(
    !concepts::ReadableMetric<DescriptiveStatistics<
        ConcurrencyPolicy::SingleThreaded, AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
    "a WriteOnly statistics instrument must NOT satisfy the ReadableMetric concept");
static_assert(
    !detail::HasRead<DescriptiveStatistics<ConcurrencyPolicy::SingleThreaded,
                                           AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
    "a WriteOnly statistics instrument must not expose read()");
static_assert(
    !detail::HasSnapshot<DescriptiveStatistics<ConcurrencyPolicy::SingleThreaded,
                                               AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
    "a WriteOnly statistics instrument must not expose snapshot()");

/// @brief Free `to_string` helper mirroring `DescriptiveStatistics::to_string()`.
///
/// @tparam Concurrency Concurrency policy of the instrument.
/// @tparam Access Access policy of the instrument.
/// @tparam Reset Reset policy of the instrument.
/// @param statistics Instrument to render.
/// @return Diagnostic text, equal to `statistics.to_string()`.
/// @note Project mandate 9 (free `to_string` for every public type).
/// @note Spec `metrics/descriptive-statistics`, requirement "Statistics
///       serialization contract".
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
[[nodiscard]] std::string
to_string(const nexigen::metrics::DescriptiveStatistics<Concurrency, Access, Reset>& statistics) {
    return statistics.to_string();
}

} // namespace nexigen::metrics

/// @brief `std::format` support for `DescriptiveStatistics`.
///
/// @tparam Concurrency Concurrency policy of the formatted instrument.
/// @tparam Access Access policy of the formatted instrument.
/// @tparam Reset Reset policy of the formatted instrument.
/// @note Project mandate 11 (`std::formatter` for every public type).
/// @note Spec `metrics/descriptive-statistics`, requirement "Statistics
///       serialization contract".
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::DescriptiveStatistics<Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    /// @brief Format the instrument by delegating to its diagnostic string.
    ///
    /// @tparam Ctx Format-context type supplied by `std::format`.
    /// @param statistics Instrument to render.
    /// @param ctx Output context the text is written to.
    /// @return Iterator past the last character written.
    template <class Ctx>
    auto
    format(const nexigen::metrics::DescriptiveStatistics<Concurrency, Access, Reset>& statistics,
           Ctx& ctx) const {
        const std::string text = statistics.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
