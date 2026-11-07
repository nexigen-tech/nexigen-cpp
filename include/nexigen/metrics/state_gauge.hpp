// nexigen::metrics — the state gauge instrument (change add-state-gauge-metric;
// spec metrics/state-gauge).
//
// StateGauge<Concurrency, Access, Reset> records a current named state — a
// bounded string such as a mode, phase, or enum spelling — as a last-writer-
// wins value. `set` copies the value into a fixed inline buffer and rejects
// empty / over-long input (63 chars, mirrors max_name_length). There is no
// arithmetic (no add/sub): a state is a current value, not an aggregate.
//
//   SingleThreaded — one plain char buffer; single-writer contract as the
//       counter's (reads must not race recording without a happens-before).
//   MultiThreaded  — a seqlock (sequence counter + atomic byte cells) with a
//       writer spinlock: readers RETRY a torn window (bounded spin) and
//       never observe torn bytes — they are lock-free under a busy writer,
//       not wait-free; writers are serialized (control-plane `set`,
//       bounded spin).
//
// ResetPolicy::Cumulative keeps the value until the next set; ResetOnRead
// reports the value and clears it to empty. Zero heap allocation anywhere.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <nexigen/metrics/concepts/metric_concept.hpp>
#include <nexigen/metrics/detail/instrument_helpers.hpp>
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/detail/probes.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
          AccessPolicy Access = AccessPolicy::Readable, ResetPolicy Reset = ResetPolicy::Cumulative>
class StateGauge;

/// Construction guard: only a Meter may create instruments.
class MeterPasskey;

namespace detail {

/// Single-threaded state cell: one plain inline buffer.
struct alignas(64) StateCell {
    char value[max_state_length + 1]{};
};

/// Multi-threaded state cell: a seqlock (sequence counter + atomic bytes).
struct alignas(64) StateShard {
    std::atomic<std::uint64_t> seq{0};
    std::atomic<char> value[max_state_length + 1]{};
};

/// State storage selected by the concurrency policy.
template <ConcurrencyPolicy Concurrency, ResetPolicy Reset> class StateStorage;

template <ResetPolicy Reset> class StateStorage<ConcurrencyPolicy::SingleThreaded, Reset> {
public:
    bool set(std::string_view value) noexcept {
        if (value.empty() || value.size() > max_state_length) {
            return false;
        }
        std::memcpy(cell_.value, value.data(), value.size());
        cell_.value[value.size()] = '\0';
        return true;
    }

    /// Copy the current value into `dst` (NUL-terminated); clears it first
    /// under ResetOnRead. Returns the length.
    std::size_t take(char (&dst)[max_state_length + 1]) const noexcept {
        const std::size_t len = copy(dst);
        if constexpr (Reset == ResetPolicy::ResetOnRead) {
            cell_.value[0] = '\0';
        }
        return len;
    }

    /// Copy the current value without clearing. Returns the length.
    std::size_t observe(char (&dst)[max_state_length + 1]) const noexcept { return copy(dst); }

private:
    std::size_t copy(char (&dst)[max_state_length + 1]) const noexcept {
        std::size_t len = 0;
        while (len < max_state_length && cell_.value[len] != '\0') {
            dst[len] = cell_.value[len];
            ++len;
        }
        dst[len] = '\0';
        return len;
    }

    mutable StateCell cell_{};
};

template <ResetPolicy Reset> class StateStorage<ConcurrencyPolicy::MultiThreaded, Reset> {
public:
    bool set(std::string_view value) noexcept {
        if (value.empty() || value.size() > max_state_length) {
            return false;
        }
        lock_and_write(value);
        return true;
    }

    std::size_t take(char (&dst)[max_state_length + 1]) const noexcept {
        const std::size_t len = copy_under_seqlock(dst);
        if constexpr (Reset == ResetPolicy::ResetOnRead) {
            clear_under_lock();
        }
        return len;
    }

    std::size_t observe(char (&dst)[max_state_length + 1]) const noexcept {
        return copy_under_seqlock(dst);
    }

private:
    std::size_t copy_under_seqlock(char (&dst)[max_state_length + 1]) const noexcept {
        for (;;) {
            const std::uint64_t s = shard_.seq.load(std::memory_order_acquire);
            if ((s & 1U) != 0U) {
                continue; // a writer is mid-update: retry
            }
            std::size_t len = 0;
            while (len <= max_state_length) {
                const char c = shard_.value[len].load(std::memory_order_relaxed);
                dst[len] = c;
                if (c == '\0') {
                    break;
                }
                ++len;
            }
            // Keep the value reads inside the checked window: compiler barrier
            // only (x86 TSO already orders loads; design D9).
            std::atomic_signal_fence(std::memory_order_acq_rel);
            if (shard_.seq.load(std::memory_order_acquire) == s) {
                return len;
            }
        }
    }

    void lock_and_write(std::string_view value) noexcept {
        while (write_lock_.test_and_set(std::memory_order_acquire)) {}
        const std::uint64_t s = shard_.seq.load(std::memory_order_relaxed);
        // Announce as a full barrier (xchg on x86): the update below must not
        // become visible before the announcement, or a reader's even-seq check
        // can pass mid-record and tear the value (design D9/D15).
        shard_.seq.store(s + 1U, std::memory_order_seq_cst); // odd: announce
        for (std::size_t i = 0; i < value.size(); ++i) {
            shard_.value[i].store(value[i], std::memory_order_relaxed);
        }
        shard_.value[value.size()].store('\0', std::memory_order_relaxed);
        shard_.seq.store(s + 2U, std::memory_order_release); // even: publish
        write_lock_.clear(std::memory_order_release);
    }

    void clear_under_lock() const noexcept {
        while (write_lock_.test_and_set(std::memory_order_acquire)) {}
        const std::uint64_t s = shard_.seq.load(std::memory_order_relaxed);
        shard_.seq.store(s + 1U, std::memory_order_seq_cst); // odd: announce
        shard_.value[0].store('\0', std::memory_order_relaxed);
        shard_.seq.store(s + 2U, std::memory_order_release); // even: publish
        write_lock_.clear(std::memory_order_release);
    }

    mutable StateShard shard_{};
    mutable std::atomic_flag write_lock_{};
};

} // namespace detail

/// A current named state. Constructible only through a Meter.
template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset> class StateGauge {
    static_assert(Concurrency == ConcurrencyPolicy::SingleThreaded ||
                      Concurrency == ConcurrencyPolicy::MultiThreaded,
                  "unknown concurrency policy");

public:
    static constexpr MetricKind kind = MetricKind::State;
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    static constexpr AccessPolicy access = Access;
    static constexpr ResetPolicy reset = Reset;

    StateGauge(const StateGauge&) = delete;
    StateGauge& operator=(const StateGauge&) = delete;
    StateGauge(StateGauge&&) = delete;
    StateGauge& operator=(StateGauge&&) = delete;

    /// Record the current state. Returns false (no side effect) for an empty
    /// or over-long (longer than 63 characters) value.
    bool set(std::string_view value) noexcept { return storage_.set(value); }

    /// The current state (or the window value for ResetOnRead — which also
    /// clears it). Available only for Readable instruments.
    [[nodiscard]] std::string_view read() const
        requires(Access == AccessPolicy::Readable)
    {
        // The view borrows this thread's own scratch (analysis #19): it is
        // valid until the NEXT read() call on this thread, not for arbitrary
        // lifetimes — copy if the value must outlive an interleaved read.
        thread_local char buffer[max_state_length + 1];
        const std::size_t len = storage_.take(buffer);
        return std::string_view(buffer, len);
    }

    /// Full record for this instrument (identity, classification, value).
    [[nodiscard]] MetricRecord snapshot() const
        requires(Access == AccessPolicy::Readable)
    {
        MetricRecord record = detail::make_record(kind, concurrency, access, reset, identity_);
        static_cast<void>(storage_.take(record.state.value));
        return record;
    }

    /// Cold-path diagnostic (mandate 9). Never resets a window.
    [[nodiscard]] std::string to_string() const {
        char buffer[max_state_length + 1];
        const std::size_t len = storage_.observe(buffer);
        std::string out = "StateGauge(kind=";
        detail::append_classification(out, kind, concurrency, access, reset);
        out += ", value=" + std::string(buffer, len);
        out += ")";
        return out;
    }

    /// Diagnostic JSON (cold path). The instrument is NOT deserializable: its
    /// construction is Meter-only.
    friend void to_json(nlohmann::json& j, const StateGauge& gauge) {
        char buffer[max_state_length + 1];
        const std::size_t len = gauge.storage_.observe(buffer);
        j = detail::classification_json(StateGauge::kind, StateGauge::concurrency,
                                        StateGauge::access, StateGauge::reset);
        j["value"] = std::string(buffer, len);
    }

private:
    friend class Meter;
    friend class MetricProvider;

    explicit StateGauge(const MeterPasskey&) noexcept {}

    /// Type-erased harvest hook: fills only the payload (classification and
    /// identity are written by the provider's harvest loop from the slot).
    static void harvest_thunk(void* object, MetricRecord& record) noexcept {
        const auto& self = *static_cast<const StateGauge*>(object);
        static_cast<void>(self.storage_.take(record.state.value));
    }

    /// Identity of the owning registry slot (set by the Meter at creation).
    const MetricIdentity* identity_{nullptr};

    detail::StateStorage<Concurrency, Reset> storage_{};
};

static_assert(sizeof(StateGauge<ConcurrencyPolicy::MultiThreaded, AccessPolicy::Readable,
                                ResetPolicy::ResetOnRead>) <= detail::max_state_size,
              "state gauge must fit the registry budget");
static_assert(concepts::Metric<StateGauge<ConcurrencyPolicy::SingleThreaded, AccessPolicy::Readable,
                                          ResetPolicy::Cumulative>>,
              "StateGauge must satisfy the Metric concept");
static_assert(concepts::ReadableMetric<StateGauge<ConcurrencyPolicy::SingleThreaded,
                                                  AccessPolicy::Readable, ResetPolicy::Cumulative>>,
              "a Readable state gauge must satisfy the ReadableMetric concept");
static_assert(
    !concepts::ReadableMetric<StateGauge<ConcurrencyPolicy::SingleThreaded, AccessPolicy::WriteOnly,
                                         ResetPolicy::Cumulative>>,
    "a WriteOnly state gauge must NOT satisfy the ReadableMetric concept");
static_assert(!detail::HasRead<StateGauge<ConcurrencyPolicy::SingleThreaded,
                                          AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
              "a WriteOnly state gauge must not expose read()");
static_assert(!detail::HasSnapshot<StateGauge<ConcurrencyPolicy::SingleThreaded,
                                              AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
              "a WriteOnly state gauge must not expose snapshot()");

/// Free helper (mandate 9): mirrors the member.
template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
[[nodiscard]] std::string to_string(const StateGauge<Concurrency, Access, Reset>& gauge) {
    return gauge.to_string();
}

} // namespace nexigen::metrics

/// State gauges format as their diagnostic string (mandate 11).
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::StateGauge<Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::StateGauge<Concurrency, Access, Reset>& gauge,
                Ctx& ctx) const {
        const std::string text = gauge.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
