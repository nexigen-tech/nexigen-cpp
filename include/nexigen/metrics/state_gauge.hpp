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

/// @brief Current-named-state instrument, forward-declared before its storage helpers.
///
/// The definition below carries the full contract; the declaration lets
/// `detail::StateStorage` and the `Meter` friend name the type.
///
/// @tparam Concurrency Concurrency policy: `SingleThreaded` or `MultiThreaded`.
/// @tparam Access Access policy: `Readable` or `WriteOnly`.
/// @tparam Reset Reset policy: `Cumulative` or `ResetOnRead`.
template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
          AccessPolicy Access = AccessPolicy::Readable, ResetPolicy Reset = ResetPolicy::Cumulative>
class StateGauge;

/// @brief Construction guard proving that only a `Meter` creates instruments.
///
/// The type stays incomplete in this header: instruments take it by reference
/// in a private constructor, so application code cannot fabricate one and
/// every instrument remains registry-owned for its whole lifetime.
class MeterPasskey;

namespace detail {

/// @brief Single-threaded state cell: one plain inline buffer, own cache line.
///
/// `max_state_length + 1` bytes hold the value and its terminator. No atomic
/// operations are used; the instrument's single-writer contract covers it.
struct alignas(64) StateCell {
    char value[max_state_length + 1]{}; ///< NUL-terminated state, never heap-allocated.
};

/// @brief Multi-threaded state cell: a seqlock over atomic bytes, own cache line.
///
/// `seq` is even when no writer is active and odd while one is mid-update;
/// readers retry a torn window instead of observing mixed bytes. The bytes
/// stay atomic so the access pattern is TSan-clean, not because the protocol
/// relies on per-byte ordering.
struct alignas(64) StateShard {
    std::atomic<std::uint64_t> seq{0}; ///< Seqlock sequence: odd while a writer is active.
    std::atomic<char> value[max_state_length + 1]{}; ///< State bytes, published by the seqlock.
};

/// @brief State storage selected by the concurrency policy.
///
/// `SingleThreaded` resolves to plain inline bytes; `MultiThreaded` resolves
/// to the seqlock cell. Both expose `set()`, `take()` and `observe()` with the
/// same signatures, so `StateGauge` carries no policy branches on the call
/// surface.
///
/// @tparam Concurrency Concurrency policy selecting the specialization.
/// @tparam Reset Reset policy controlling whether `take()` clears the value.
template <ConcurrencyPolicy Concurrency, ResetPolicy Reset> class StateStorage;

/// @brief Plain single-writer state storage.
///
/// `set()` rejects empty or over-long input without touching the stored value.
/// Reads and harvests must not race `set()` without a happens-before edge.
///
/// @tparam Reset Reset policy; `ResetOnRead` makes `take()` clear the value.
template <ResetPolicy Reset> class StateStorage<ConcurrencyPolicy::SingleThreaded, Reset> {
public:
    /// @brief Copy `value` into the inline buffer.
    ///
    /// @param value State to store; must be non-empty and at most
    ///        `max_state_length` characters.
    /// @return `true` when stored, `false` for empty or over-long input with
    ///         no side effect.
    /// @note Spec `metrics/state-gauge`, requirement "State recording surface".
    bool set(std::string_view value) noexcept {
        if (value.empty() || value.size() > max_state_length) {
            return false;
        }
        std::memcpy(cell_.value, value.data(), value.size());
        cell_.value[value.size()] = '\0';
        return true;
    }

    /// @brief Copy the current value into `dst` and, under `ResetOnRead`, clear it.
    ///
    /// @param dst Destination buffer of `max_state_length + 1` bytes that
    ///        receives a NUL-terminated copy.
    /// @return Length of the copied value in bytes, excluding the terminator.
    /// @note Spec `metrics/state-gauge`, requirement "State reset windows".
    std::size_t take(char (&dst)[max_state_length + 1]) const noexcept {
        const std::size_t len = copy(dst);
        if constexpr (Reset == ResetPolicy::ResetOnRead) {
            cell_.value[0] = '\0';
        }
        return len;
    }

    /// @brief Copy the current value into `dst` without clearing it.
    ///
    /// @param dst Destination buffer of `max_state_length + 1` bytes that
    ///        receives a NUL-terminated copy.
    /// @return Length of the copied value in bytes, excluding the terminator.
    std::size_t observe(char (&dst)[max_state_length + 1]) const noexcept { return copy(dst); }

private:
    /// @brief Copy the stored bytes into `dst`, stopping at the terminator.
    ///
    /// @param dst Destination buffer of `max_state_length + 1` bytes.
    /// @return Number of bytes copied, excluding the terminator.
    std::size_t copy(char (&dst)[max_state_length + 1]) const noexcept {
        std::size_t len = 0;
        while (len < max_state_length && cell_.value[len] != '\0') {
            dst[len] = cell_.value[len];
            ++len;
        }
        dst[len] = '\0';
        return len;
    }

    mutable StateCell cell_{}; ///< The single plain buffer; mutable so `take()` can clear.
};

/// @brief Seqlock-protected state storage for multiple writers.
///
/// Writers are serialized by a spinlock (control-plane `set`), while readers
/// stay lock-free: they retry a torn window with a bounded spin and never
/// observe a mix of two values. `take()` under `ResetOnRead` clears the value
/// while holding the writer lock.
///
/// @tparam Reset Reset policy; `ResetOnRead` makes `take()` clear the value.
/// @note Spec `metrics/state-gauge`, requirement "Multi-threaded
///       last-writer-wins".
template <ResetPolicy Reset> class StateStorage<ConcurrencyPolicy::MultiThreaded, Reset> {
public:
    /// @brief Publish `value` as the current state.
    ///
    /// @param value State to store; must be non-empty and at most
    ///        `max_state_length` characters.
    /// @return `true` when stored, `false` for empty or over-long input with
    ///         no side effect.
    /// @note Spec `metrics/state-gauge`, requirement "State recording surface".
    bool set(std::string_view value) noexcept {
        if (value.empty() || value.size() > max_state_length) {
            return false;
        }
        lock_and_write(value);
        return true;
    }

    /// @brief Copy the current value into `dst` and, under `ResetOnRead`, clear it.
    ///
    /// @param dst Destination buffer of `max_state_length + 1` bytes that
    ///        receives a complete, NUL-terminated copy.
    /// @return Length of the copied value in bytes, excluding the terminator.
    /// @note Spec `metrics/state-gauge`, requirement "State reset windows".
    std::size_t take(char (&dst)[max_state_length + 1]) const noexcept {
        const std::size_t len = copy_under_seqlock(dst);
        if constexpr (Reset == ResetPolicy::ResetOnRead) {
            clear_under_lock();
        }
        return len;
    }

    /// @brief Copy the current value into `dst` without clearing it.
    ///
    /// @param dst Destination buffer of `max_state_length + 1` bytes that
    ///        receives a complete, NUL-terminated copy.
    /// @return Length of the copied value in bytes, excluding the terminator.
    std::size_t observe(char (&dst)[max_state_length + 1]) const noexcept {
        return copy_under_seqlock(dst);
    }

private:
    /// @brief Retry until one complete value is read under a stable sequence.
    ///
    /// An odd sequence means a writer is mid-update, so the read restarts; the
    /// before/after sequence comparison proves no writer touched the bytes in
    /// between. The loop is lock-free but not wait-free under a busy writer.
    ///
    /// @param dst Destination buffer of `max_state_length + 1` bytes.
    /// @return Length of the copied value in bytes, excluding the terminator.
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

    /// @brief Serialize writers, announce the update, store the bytes, publish.
    ///
    /// The spinlock serializes writers only; readers retry instead of taking
    /// it. The odd sequence is published as a full barrier so no byte store can
    /// become visible before the announcement.
    ///
    /// @param value State already validated as non-empty and in range.
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

    /// @brief Clear the stored value through the same seqlock handshake.
    ///
    /// Takes the writer spinlock, publishes an odd sequence, stores the empty
    /// terminator, then publishes an even sequence.
    void clear_under_lock() const noexcept {
        while (write_lock_.test_and_set(std::memory_order_acquire)) {}
        const std::uint64_t s = shard_.seq.load(std::memory_order_relaxed);
        shard_.seq.store(s + 1U, std::memory_order_seq_cst); // odd: announce
        shard_.value[0].store('\0', std::memory_order_relaxed);
        shard_.seq.store(s + 2U, std::memory_order_release); // even: publish
        write_lock_.clear(std::memory_order_release);
    }

    mutable StateShard shard_{};            ///< Seqlock-protected value bytes and sequence.
    mutable std::atomic_flag write_lock_{}; ///< Spinlock that serializes writers only.
};

} // namespace detail

/// @brief Current named state with compile-time concurrency, access and reset policies.
///
/// Records a bounded string — a mode, phase, or enum spelling — as a
/// last-writer-wins value. `set()` copies the value into fixed inline storage
/// and rejects empty or over-long input; there is no arithmetic because a
/// state is a current value, not an aggregate. The storage strategy is chosen
/// at compile time: `SingleThreaded` keeps plain inline bytes, while
/// `MultiThreaded` publishes each value through a seqlock so a reader never
/// observes a torn mix of two writers. Construction is `Meter`-only; the type
/// is neither copyable nor movable, and the registry owns its storage for the
/// instrument lifetime.
///
/// `set()` is the recording surface; `read()`, `snapshot()`, harvesting and
/// `to_string()` belong to the cold path. Nothing here allocates on the heap.
///
/// @tparam Concurrency Concurrency policy, `ConcurrencyPolicy::SingleThreaded`
///         or `ConcurrencyPolicy::MultiThreaded`; any other value fails the
///         `static_assert`.
/// @tparam Access Access policy. `Readable` exposes `read()`/`snapshot()`;
///         `WriteOnly` removes them at compile time.
/// @tparam Reset Reset policy. `Cumulative` keeps the value until the next
///         `set()`; `ResetOnRead` reports it and clears it to empty.
/// @note Spec `metrics/state-gauge`, requirement "State recording surface".
/// @note Spec `metrics/state-gauge`, requirement "Multi-threaded
///       last-writer-wins".
/// @note Satisfies `concepts::Metric` and, for
///       `Access == AccessPolicy::Readable` only,
///       `concepts::ReadableMetric` (spec `metrics/metric-concepts`,
///       requirement "Readable metric concept").
/// @warning Under `ConcurrencyPolicy::SingleThreaded` there is one writer:
///          reads and harvests must not run concurrently with recording
///          unless the caller establishes happens-before (for example with
///          `collect_now()` on the recording thread).
template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset> class StateGauge {
    static_assert(Concurrency == ConcurrencyPolicy::SingleThreaded ||
                      Concurrency == ConcurrencyPolicy::MultiThreaded,
                  "unknown concurrency policy");

public:
    /// @brief Metric kind discriminant reported in every snapshot.
    static constexpr MetricKind kind = MetricKind::State;
    /// @brief Concurrency policy this instantiation was built with.
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    /// @brief Access policy controlling whether the read side is exposed.
    static constexpr AccessPolicy access = Access;
    /// @brief Reset policy controlling cumulative versus windowed reporting.
    static constexpr ResetPolicy reset = Reset;

    /// @brief Copy construction is deleted: instruments are registry-owned.
    StateGauge(const StateGauge&) = delete;
    /// @brief Copy assignment is deleted: instruments are registry-owned.
    StateGauge& operator=(const StateGauge&) = delete;
    /// @brief Move construction is deleted: instrument addresses are stable.
    StateGauge(StateGauge&&) = delete;
    /// @brief Move assignment is deleted: instrument addresses are stable.
    StateGauge& operator=(StateGauge&&) = delete;

    /// @brief Record the current state.
    ///
    /// @param value State to store; must be non-empty and at most 63
    ///        characters (`max_state_length`).
    /// @return `true` when stored. `false` (with no side effect) for an empty
    ///         or over-long value; the previous state is unchanged.
    /// @note Spec `metrics/state-gauge`, requirement "State recording surface".
    bool set(std::string_view value) noexcept { return storage_.set(value); }

    /// @brief Current state, or the window value under `ResetOnRead`.
    ///
    /// Under `ResetPolicy::ResetOnRead` the call consumes the window: the
    /// reported state is returned once and the next call observes an empty
    /// view.
    ///
    /// @return Borrowed view of the current state; empty when nothing is set.
    /// @note Spec `metrics/state-gauge`, requirement "State reset windows".
    /// @note Available only when `Access == AccessPolicy::Readable` (spec
    ///       `metrics/metric-concepts`, requirement "Compile-time access
    ///       gating").
    /// @warning The view borrows thread-local scratch that is reused by the
    ///          next `read()` on the same thread; copy it if it must outlive
    ///          an interleaved read.
    [[nodiscard]] std::string_view read() const
        requires(Access == AccessPolicy::Readable)
    {
        thread_local char buffer[max_state_length + 1];
        const std::size_t len = storage_.take(buffer);
        return std::string_view(buffer, len);
    }

    /// @brief Build the full `MetricRecord` for this instrument.
    ///
    /// Carries `MetricKind::State`, the meter and metric identity, the policy
    /// classification and the current value. Under `ResetOnRead` it consumes
    /// the window exactly like `read()`.
    ///
    /// @return Record with classification filled in and `state.value` set.
    /// @note Spec `metrics/state-gauge`, requirement "State read surface".
    /// @note Available only when `Access == AccessPolicy::Readable` (spec
    ///       `metrics/metric-concepts`, requirement "Compile-time access
    ///       gating").
    [[nodiscard]] MetricRecord snapshot() const
        requires(Access == AccessPolicy::Readable)
    {
        MetricRecord record = detail::make_record(kind, concurrency, access, reset, identity_);
        static_cast<void>(storage_.take(record.state.value));
        return record;
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// The text carries the kind, the policy classification and the current
    /// state. It never resets a `ResetOnRead` window.
    ///
    /// @return Diagnostic text; the free `to_string()` overload returns the
    ///         same string.
    /// @note Project mandate 9 (member and free `to_string`).
    /// @note Spec `metrics/state-gauge`, requirement "State serialization
    ///       contract".
    [[nodiscard]] std::string to_string() const {
        char buffer[max_state_length + 1];
        const std::size_t len = storage_.observe(buffer);
        std::string out = "StateGauge(kind=";
        detail::append_classification(out, kind, concurrency, access, reset);
        out += ", value=" + std::string(buffer, len);
        out += ")";
        return out;
    }

    /// @brief Serialize the state gauge as diagnostic JSON.
    ///
    /// The object carries the classification fields plus a `"value"` string.
    /// It is deliberately write-only: there is no `from_json`, because an
    /// instrument can only be created by a `Meter`.
    ///
    /// @param j JSON value to assign to.
    /// @param gauge State gauge being serialized.
    /// @note Project mandate 10 (non-throwing `to_json` under
    ///       `JSON_NOEXCEPTION=1`).
    /// @note Spec `metrics/state-gauge`, requirement "State serialization
    ///       contract".
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

    /// @brief Create an empty state gauge; the unnamed passkey proves `Meter` origin.
    ///
    /// The argument is intentionally unnamed: it only has to exist, and it can
    /// only be produced by `Meter`, so no other code path can construct an
    /// instrument.
    explicit StateGauge(const MeterPasskey&) noexcept {}

    /// @brief Type-erased harvest hook used by the provider's plain C function pointer.
    ///
    /// @param object Instrument addressed as `void*` by the registry slot.
    /// @param record Record whose `state.value` receives the window value.
    /// @note Spec `metrics/state-gauge`, requirement "State reset windows".
    static void harvest_thunk(void* object, MetricRecord& record) noexcept {
        const auto& self = *static_cast<const StateGauge*>(object);
        static_cast<void>(self.storage_.take(record.state.value));
    }

    /// @brief Identity of the owning registry slot, set by the `Meter` at creation.
    const MetricIdentity* identity_{nullptr};

    /// @brief Policy-selected storage; the only state of the instrument.
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

/// @brief Free `to_string` helper mirroring `StateGauge::to_string()`.
///
/// @tparam Concurrency Concurrency policy of the gauge.
/// @tparam Access Access policy of the gauge.
/// @tparam Reset Reset policy of the gauge.
/// @param gauge State gauge to render.
/// @return Diagnostic text, equal to `gauge.to_string()`.
/// @note Project mandate 9 (free `to_string` for every public type).
/// @note Spec `metrics/state-gauge`, requirement "State serialization
///       contract".
template <ConcurrencyPolicy Concurrency, AccessPolicy Access, ResetPolicy Reset>
[[nodiscard]] std::string to_string(const StateGauge<Concurrency, Access, Reset>& gauge) {
    return gauge.to_string();
}

} // namespace nexigen::metrics

/// @brief `std::format` support for `StateGauge`, rendering its diagnostic string.
///
/// @tparam Concurrency Concurrency policy of the formatted gauge.
/// @tparam Access Access policy of the formatted gauge.
/// @tparam Reset Reset policy of the formatted gauge.
/// @note Project mandate 11 (`std::formatter` for every public type).
/// @note Spec `metrics/state-gauge`, requirement "State serialization
///       contract".
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::StateGauge<Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    /// @brief Format a state gauge by delegating to its diagnostic string.
    ///
    /// @tparam Ctx Format-context type supplied by `std::format`.
    /// @param gauge State gauge to render.
    /// @param ctx Output context the text is written to.
    /// @return Iterator past the last character written.
    template <class Ctx>
    auto format(const nexigen::metrics::StateGauge<Concurrency, Access, Reset>& gauge,
                Ctx& ctx) const {
        const std::string text = gauge.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
