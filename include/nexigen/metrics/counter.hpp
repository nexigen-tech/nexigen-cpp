// nexigen::metrics — the counter instrument (design D8;
// spec metrics/counter-gauge).
//
// Counter<Concurrency, Access, Reset> counts events. Two storage shapes:
//
//   SingleThreaded — one plain 64-bit cell on its own cache line. inc() is a
//       plain increment (sub-nanosecond class, no atomic instruction). The
//       contract is single-writer: reads and harvests must not run
//       concurrently with recording unless the caller establishes
//       happens-before (use collect_now() on the recording thread).
//
//   MultiThreaded  — 32 exclusive per-thread shards plus one shared fallback
//       cell. Recording is a relaxed fetch_add on the caller's own shard:
//       wait-free, no cross-core CAS contention, no cache line shared
//       between threads.
//
// ResetPolicy::ResetOnRead reports the delta since the previous observation
// and clears the window; every increment is a single atomic RMW, so it lands
// in exactly one window.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <nexigen/metrics/concepts/metric_concept.hpp>
#include <nexigen/metrics/detail/instrument_helpers.hpp>
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/detail/probes.hpp>
#include <nexigen/metrics/detail/sharded_scalar.hpp>
#include <nexigen/metrics/detail/thread_slot.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

/// @brief Counted-event instrument, forward-declared before its storage helpers.
///
/// The definition below carries the full contract; the declaration exists so
/// `detail::CounterStorage` and the `Meter` friend can name the type.
///
/// @tparam Concurrency Concurrency policy: `SingleThreaded` or `MultiThreaded`.
/// @tparam Access Access policy: `Readable` or `WriteOnly`.
/// @tparam Reset Reset policy: `Cumulative` or `ResetOnRead`.
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
class Counter;

/// @brief Construction guard proving that only a `Meter` creates instruments.
///
/// The type stays incomplete in this header: instruments take it by reference
/// in a private constructor, so application code cannot fabricate one and
/// every instrument remains registry-owned for its whole lifetime.
class MeterPasskey;

namespace detail {

/// @brief Single-threaded counter cell: one plain 64-bit value, own cache line.
///
/// No atomic operations and no padding beyond the alignment: recording is a
/// plain increment, and a different instrument on the same thread avoids the
/// line because every cell is `alignas(64)`.
struct alignas(64) CounterCell {
    std::uint64_t value{0}; ///< Accumulated count, plain (non-atomic) storage.
};

// Shard size invariant: one multi-threaded counter shard owns one cache line.
static_assert(sizeof(detail::ScalarShard<std::uint64_t>) == 64,
              "a counter shard owns exactly one cache line");

/// @brief Counter storage selected by the concurrency policy.
///
/// `SingleThreaded` resolves to a plain `CounterCell`; `MultiThreaded`
/// resolves to a `detail::ShardedScalar` with the `MetricKind::Counter` slot
/// space. Both expose `add()`, `observe()` and `take()`, so `Counter` itself
/// contains no policy branches on the recording path.
///
/// @tparam Concurrency Concurrency policy selecting the specialization.
/// @tparam Reset Reset policy controlling whether `take()` clears the window.
template <ConcurrencyPolicy Concurrency, ResetPolicy Reset> class CounterStorage;

/// @brief Plain single-writer counter storage.
///
/// Recording is a non-atomic store, so the recording thread must be the only
/// writer. `take()` clears the cell only when `Reset` is
/// `ResetPolicy::ResetOnRead`.
///
/// @tparam Reset Reset policy; `ResetOnRead` makes `take()` consume the window.
template <ResetPolicy Reset> class CounterStorage<ConcurrencyPolicy::SingleThreaded, Reset> {
public:
    /// @brief Add `delta` to the stored count.
    ///
    /// @param delta Number of events to add; applied with plain arithmetic.
    /// @warning Single-writer: call from the instrument's recording thread.
    void add(std::uint64_t delta) noexcept { cell_.value += delta; }

    /// @brief Read the stored count without changing it.
    ///
    /// @return Current accumulated count.
    [[nodiscard]] std::uint64_t observe() const noexcept { return cell_.value; }

    /// @brief Read the window and, under `ResetOnRead`, clear it.
    ///
    /// @return Accumulated count, or the delta since the previous `take()`.
    /// @note Spec `metrics/counter-gauge`, requirement "Counter reset windows".
    [[nodiscard]] std::uint64_t take() const noexcept {
        if constexpr (Reset == ResetPolicy::ResetOnRead) {
            const std::uint64_t value = cell_.value;
            cell_.value = 0;
            return value;
        } else {
            return cell_.value;
        }
    }

private:
    mutable CounterCell cell_{}; ///< The single plain cell; mutable so `take()` can clear.
};

/// @brief Multi-threaded counter storage: per-thread shards plus a fallback.
///
/// Inherits `detail::ShardedScalar`, so `add()` is a relaxed `fetch_add` on
/// the caller's own shard and `observe()`/`take()` aggregate all shards.
///
/// @tparam Reset Reset policy forwarded to the sharded scalar.
/// @note Spec `metrics/counter-gauge`, requirement "Counter multi-threaded
///       sharding".
template <ResetPolicy Reset>
class CounterStorage<ConcurrencyPolicy::MultiThreaded, Reset>
    : public detail::ShardedScalar<std::uint64_t, MetricKind::Counter, Reset> {};

} // namespace detail

/// @brief Counted-event instrument with compile-time concurrency, access and reset policies.
///
/// `Counter` accumulates integer events. The storage strategy is chosen at
/// compile time: `SingleThreaded` keeps one plain 64-bit cell on its own
/// cache line, while `MultiThreaded` uses 32 cache-line-aligned per-thread
/// shards plus a shared fallback cell, so recording never contends for a
/// cache line across cores. Construction is `Meter`-only; the type is neither
/// copyable nor movable, and the registry owns its storage for the instrument
/// lifetime.
///
/// `inc()` and `add()` are the wait-free recording path; `read()`,
/// `snapshot()`, harvesting and `to_string()` belong to the cold path.
///
/// @tparam Concurrency Concurrency policy, `ConcurrencyPolicy::SingleThreaded`
///         or `ConcurrencyPolicy::MultiThreaded`; any other value fails the
///         `static_assert`.
/// @tparam Access Access policy. `Readable` exposes `read()`/`snapshot()`;
///         `WriteOnly` removes them at compile time.
/// @tparam Reset Reset policy. `Cumulative` keeps totals across observations;
///         `ResetOnRead` reports and clears the current window.
/// @note Spec `metrics/counter-gauge`, requirement "Counter recording surface".
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
class Counter {
    static_assert(Concurrency == ConcurrencyPolicy::SingleThreaded ||
                      Concurrency == ConcurrencyPolicy::MultiThreaded,
                  "unknown concurrency policy");

public:
    /// @brief Metric kind discriminant reported in every snapshot.
    static constexpr MetricKind kind = MetricKind::Counter;
    /// @brief Concurrency policy this instantiation was built with.
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    /// @brief Access policy controlling whether the read side is exposed.
    static constexpr AccessPolicy access = Access;
    /// @brief Reset policy controlling cumulative versus windowed reporting.
    static constexpr ResetPolicy reset = Reset;

    /// @brief Copy construction is deleted: instruments are registry-owned.
    Counter(const Counter&) = delete;
    /// @brief Copy assignment is deleted: instruments are registry-owned.
    Counter& operator=(const Counter&) = delete;
    /// @brief Move construction is deleted: instrument addresses are stable.
    Counter(Counter&&) = delete;
    /// @brief Move assignment is deleted: instrument addresses are stable.
    Counter& operator=(Counter&&) = delete;

    /// @brief Record one event.
    ///
    /// A single increment with no allocation, locking or syscall: a plain add
    /// under `SingleThreaded` and one relaxed `fetch_add` on the caller's shard
    /// under `MultiThreaded`.
    ///
    /// @note Spec `metrics/counter-gauge`, requirement "Counter recording
    ///       surface".
    /// @note Spec `metrics/counter-gauge`, requirement "Counter multi-threaded
    ///       sharding".
    void inc() noexcept { storage_.add(1U); }

    /// @brief Record `delta` events at once.
    ///
    /// @param delta Number of events to add. It lands in exactly one reset
    ///        window, even when a harvest runs concurrently.
    /// @note Spec `metrics/counter-gauge`, requirement "Counter recording
    ///       surface".
    /// @note Spec `metrics/counter-gauge`, requirement "Counter reset windows".
    void add(std::uint64_t delta) noexcept { storage_.add(delta); }

    /// @brief Current total, or the window delta under `ResetOnRead`.
    ///
    /// Under `ResetPolicy::ResetOnRead` the call consumes the window: the
    /// reported value is returned once and accumulation restarts at zero.
    /// Under `MultiThreaded` the read aggregates every shard, so a concurrent
    /// increment is counted in this window or the next one, never in both.
    ///
    /// @return Accumulated count, or the delta since the previous observation.
    /// @note Spec `metrics/counter-gauge`, requirement "Counter reset windows".
    /// @note Available only when `Access == AccessPolicy::Readable` (spec
    ///       `metrics/metric-concepts`, requirement "Compile-time access
    ///       gating").
    [[nodiscard]] std::uint64_t read() const
        requires(Access == AccessPolicy::Readable)
    {
        return storage_.take();
    }

    /// @brief Build the full `MetricRecord` for this instrument.
    ///
    /// Carries `MetricKind::Counter`, the meter and metric identity, the
    /// policy classification, and the current scalar value. Under
    /// `ResetOnRead` it consumes the window exactly like `read()`.
    ///
    /// @return Record with classification filled in and `scalar.counter` set.
    /// @note Spec `metrics/counter-gauge`, requirement "Counter reset windows".
    /// @note Available only when `Access == AccessPolicy::Readable` (spec
    ///       `metrics/metric-concepts`, requirement "Compile-time access
    ///       gating").
    [[nodiscard]] MetricRecord snapshot() const
        requires(Access == AccessPolicy::Readable)
    {
        MetricRecord record = detail::make_record(kind, concurrency, access, reset, identity_);
        record.scalar.counter = storage_.take();
        return record;
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// The text carries the kind, the policy classification and the current
    /// aggregate. It never resets a `ResetOnRead` window.
    ///
    /// @return Diagnostic text; the free `to_string()` overload returns the
    ///         same string.
    /// @note Project mandate 9 (member and free `to_string`).
    /// @note Spec `metrics/counter-gauge`, requirement "Counter serialization
    ///       contract".
    [[nodiscard]] std::string to_string() const {
        std::string out = "Counter(kind=";
        detail::append_classification(out, kind, concurrency, access, reset);
        out += ", value=" + std::to_string(storage_.observe());
        out += ")";
        return out;
    }

    /// @brief Serialize the counter as diagnostic JSON.
    ///
    /// The object carries the classification fields plus `"value"`. It is
    /// deliberately write-only: there is no `from_json`, because an
    /// instrument can only be created by a `Meter`.
    ///
    /// @param j JSON value to assign to.
    /// @param counter Counter being serialized.
    /// @note Project mandate 10 (non-throwing `to_json` under
    ///       `JSON_NOEXCEPTION=1`).
    /// @note Spec `metrics/counter-gauge`, requirement "Counter serialization
    ///       contract".
    friend void to_json(nlohmann::json& j, const Counter& counter) {
        j = detail::classification_json(Counter::kind, Counter::concurrency, Counter::access,
                                        Counter::reset);
        j["value"] = counter.storage_.observe();
    }

private:
    friend class Meter;
    friend class MetricProvider;

    /// @brief Create an empty counter; the unnamed passkey proves `Meter` origin.
    ///
    /// The argument is intentionally unnamed: it only has to exist, and it can
    /// only be produced by `Meter`, so no other code path can construct an
    /// instrument.
    explicit Counter(const MeterPasskey&) noexcept {}

    /// @brief Type-erased harvest hook used by the provider's plain C function pointer.
    ///
    /// @param object Instrument addressed as `void*` by the registry slot.
    /// @param record Record whose `scalar.counter` receives the window value.
    /// @note Spec `metrics/counter-gauge`, requirement "Counter reset windows".
    static void harvest_thunk(void* object, MetricRecord& record) noexcept {
        const auto& self = *static_cast<const Counter*>(object);
        record.scalar.counter = self.storage_.take();
    }

    /// @brief Identity of the owning registry slot, set by the `Meter` at creation.
    const MetricIdentity* identity_{nullptr};

    /// @brief Policy-selected storage; the only state of the instrument.
    detail::CounterStorage<Concurrency, Reset> storage_{};
};

static_assert(sizeof(Counter<ConcurrencyPolicy::MultiThreaded, AccessPolicy::Readable,
                             ResetPolicy::ResetOnRead>) <= detail::max_counter_size,
              "counter must fit the registry budget");
static_assert(concepts::Metric<Counter<ConcurrencyPolicy::SingleThreaded, AccessPolicy::Readable,
                                       ResetPolicy::Cumulative>>,
              "Counter must satisfy the Metric concept");
static_assert(concepts::ReadableMetric<Counter<ConcurrencyPolicy::SingleThreaded,
                                               AccessPolicy::Readable, ResetPolicy::Cumulative>>,
              "a Readable counter must satisfy the ReadableMetric concept");
static_assert(!concepts::ReadableMetric<Counter<ConcurrencyPolicy::SingleThreaded,
                                                AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
              "a WriteOnly counter must NOT satisfy the ReadableMetric concept");
static_assert(!detail::HasRead<Counter<ConcurrencyPolicy::SingleThreaded, AccessPolicy::WriteOnly,
                                       ResetPolicy::Cumulative>>,
              "a WriteOnly counter must not expose read()");
static_assert(!detail::HasSnapshot<Counter<ConcurrencyPolicy::SingleThreaded,
                                           AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
              "a WriteOnly counter must not expose snapshot()");

/// @brief Free `to_string` helper mirroring `Counter::to_string()`.
///
/// @tparam Concurrency Concurrency policy of the counter.
/// @tparam Access Access policy of the counter.
/// @tparam Reset Reset policy of the counter.
/// @param counter Counter to render.
/// @return Diagnostic text, equal to `counter.to_string()`.
/// @note Project mandate 9 (free `to_string` for every public type).
/// @note Spec `metrics/counter-gauge`, requirement "Counter serialization
///       contract".
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
[[nodiscard]] std::string
to_string(const nexigen::metrics::Counter<Concurrency, Access, Reset>& counter) {
    return counter.to_string();
}

} // namespace nexigen::metrics

/// @brief `std::format` support for `Counter`, rendering its diagnostic string.
///
/// @tparam Concurrency Concurrency policy of the formatted counter.
/// @tparam Access Access policy of the formatted counter.
/// @tparam Reset Reset policy of the formatted counter.
/// @note Project mandate 11 (`std::formatter` for every public type).
/// @note Spec `metrics/counter-gauge`, requirement "Counter serialization
///       contract".
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::Counter<Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    /// @brief Format a counter by delegating to its diagnostic string.
    ///
    /// @tparam Ctx Format-context type supplied by `std::format`.
    /// @param counter Counter to render.
    /// @param ctx Output context the text is written to.
    /// @return Iterator past the last character written.
    template <class Ctx>
    auto format(const nexigen::metrics::Counter<Concurrency, Access, Reset>& counter,
                Ctx& ctx) const {
        const std::string text = counter.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
