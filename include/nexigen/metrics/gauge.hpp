// nexigen::metrics — the gauge instrument (design D8; spec metrics/counter-gauge).
//
// Gauge<ValueType, Concurrency, Access, Reset> holds a current value that can
// be assigned (set) or moved by a delta (add / sub). Supported value types are
// std::int64_t and double.
//
//   SingleThreaded — one plain cell on its own cache line.
//   MultiThreaded  — 32 exclusive per-thread shards plus one shared fallback
//       cell; add/sub are relaxed fetch_add on the caller's own shard (no
//       cross-core contention). set() moves the aggregate to the target value
//       as observed at call time: it reads the current aggregate and applies
//       the difference on the caller's shard, so it costs one pass over the
//       shards — a control-plane operation, not a per-sample hot path.
//
// ResetPolicy::ResetOnRead reports the net activity of the current window and
// clears it.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <type_traits>

#include <nexigen/metrics/concepts/metric_concept.hpp>
#include <nexigen/metrics/detail/instrument_helpers.hpp>
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/detail/probes.hpp>
#include <nexigen/metrics/detail/sharded_scalar.hpp>
#include <nexigen/metrics/detail/thread_slot.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

/// @brief Current-value instrument, forward-declared before its storage helpers.
///
/// The definition below carries the full contract; the declaration lets
/// `detail::GaugeStorage` and the `Meter` friend name the type.
///
/// @tparam ValueType Stored value type: `std::int64_t` or `double`.
/// @tparam Concurrency Concurrency policy: `SingleThreaded` or `MultiThreaded`.
/// @tparam Access Access policy: `Readable` or `WriteOnly`.
/// @tparam Reset Reset policy: `Cumulative` or `ResetOnRead`.
template <typename ValueType, nexigen::metrics::ConcurrencyPolicy Concurrency,
          nexigen::metrics::AccessPolicy Access, nexigen::metrics::ResetPolicy Reset>
class Gauge;

/// @brief Construction guard proving that only a `Meter` creates instruments.
///
/// The type stays incomplete in this header: instruments take it by reference
/// in a private constructor, so application code cannot fabricate one and
/// every instrument remains registry-owned for its whole lifetime.
class MeterPasskey;

namespace detail {

/// @brief Gauge storage selected by the concurrency policy.
///
/// `SingleThreaded` resolves to a plain cell; `MultiThreaded` resolves to a
/// sharded scalar with the `MetricKind::Gauge` slot space. Both expose
/// `set()`, `add()`, `observe()` and `take()`.
///
/// @tparam ValueType Stored value type: `std::int64_t` or `double`.
/// @tparam Concurrency Concurrency policy selecting the specialization.
/// @tparam Reset Reset policy controlling whether `take()` clears the window.
template <typename ValueType, ConcurrencyPolicy Concurrency, ResetPolicy Reset> class GaugeStorage;

/// @brief Plain single-writer gauge storage.
///
/// Recording is a non-atomic store, so the recording thread must be the only
/// writer. `take()` clears the cell only under `ResetPolicy::ResetOnRead`.
///
/// @tparam ValueType Stored value type, trivially copyable and numeric.
/// @tparam Reset Reset policy; `ResetOnRead` makes `take()` consume the window.
template <typename ValueType, ResetPolicy Reset>
class GaugeStorage<ValueType, ConcurrencyPolicy::SingleThreaded, Reset> {
public:
    /// @brief Assign the stored value.
    ///
    /// @param value New value; stored with a plain assignment.
    /// @warning Single-writer: call from the instrument's recording thread.
    void set(ValueType value) noexcept { cell_ = value; }

    /// @brief Move the stored value by `delta`.
    ///
    /// @param delta Signed change applied with plain arithmetic.
    /// @warning Single-writer: call from the instrument's recording thread.
    void add(ValueType delta) noexcept { cell_ = static_cast<ValueType>(cell_ + delta); }

    /// @brief Read the stored value without changing it.
    ///
    /// @return Current value.
    [[nodiscard]] ValueType observe() const noexcept { return cell_; }

    /// @brief Read the stored value and, under `ResetOnRead`, clear it to zero.
    ///
    /// @return Current value, or the net window activity since the previous call.
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge reset windows".
    [[nodiscard]] ValueType take() const noexcept {
        if constexpr (Reset == ResetPolicy::ResetOnRead) {
            const ValueType value = cell_;
            cell_ = ValueType{0};
            return value;
        } else {
            return cell_;
        }
    }

private:
    mutable ValueType cell_{}; ///< The single plain cell; mutable so `take()` can clear.
};

/// @brief Multi-threaded gauge storage: per-thread shards plus a fallback.
///
/// `add()` is a relaxed `fetch_add` on the caller's own shard, so concurrent
/// updates never contend for one cache line. `set()` is a control-plane
/// reassignment of the whole aggregate, not a per-sample operation.
///
/// @tparam ValueType Stored value type: `std::int64_t` or `double`.
/// @tparam Reset Reset policy forwarded to the sharded scalar.
/// @note Spec `metrics/counter-gauge`, requirement "Gauge recording surface".
template <typename ValueType, ResetPolicy Reset>
class GaugeStorage<ValueType, ConcurrencyPolicy::MultiThreaded, Reset>
    : public detail::ShardedScalar<ValueType, MetricKind::Gauge, Reset> {
public:
    /// @brief Move the aggregate to `value` as observed at call time.
    ///
    /// A bounded control-plane pass over the shards, not a hot-path call: the
    /// aggregate equals the target once the call completes.
    ///
    /// @param value Target value for the aggregate.
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge recording
    ///       surface".
    void set(ValueType value) noexcept {
        // Move the aggregate to `value` exactly: reassign the shards (spec
        // counter-gauge "observed at call time" reading set as a
        // control-plane reassignment — analysis #19). The former
        // observe-then-add delta was check-then-act: a concurrent writer
        // between observe() and add() skewed the final value away from the
        // target.
        this->assign(value);
    }
};

} // namespace detail

/// @brief Current-value instrument with compile-time value, access and reset policies.
///
/// `Gauge` holds one current value that can be assigned with `set()` or moved
/// with `add()`/`sub()`. The storage strategy is chosen at compile time:
/// `SingleThreaded` keeps one plain cell on its own cache line, while
/// `MultiThreaded` uses 32 cache-line-aligned per-thread shards plus a shared
/// fallback cell. Construction is `Meter`-only; the type is neither copyable
/// nor movable, and the registry owns its storage for the instrument lifetime.
///
/// `set()`, `add()` and `sub()` are the recording surface; `read()`,
/// `snapshot()`, harvesting and `to_string()` belong to the cold path.
///
/// @tparam ValueType Stored value type, `std::int64_t` or `double`; any other
///         type fails the `static_assert`.
/// @tparam Concurrency Concurrency policy, `ConcurrencyPolicy::SingleThreaded`
///         or `ConcurrencyPolicy::MultiThreaded`.
/// @tparam Access Access policy. `Readable` exposes `read()`/`snapshot()`;
///         `WriteOnly` removes them at compile time.
/// @tparam Reset Reset policy. `Cumulative` retains the aggregate across
///         observations; `ResetOnRead` reports and clears the window.
/// @note Spec `metrics/counter-gauge`, requirement "Gauge recording surface".
/// @note Satisfies `concepts::Metric` and, for
///       `Access == AccessPolicy::Readable` only,
///       `concepts::ReadableMetric` (spec `metrics/metric-concepts`,
///       requirement "Readable metric concept").
/// @warning Under `ConcurrencyPolicy::SingleThreaded` there is one writer:
///          reads and harvests must not run concurrently with recording
///          unless the caller establishes happens-before (for example with
///          `collect_now()` on the recording thread).
template <typename ValueType = std::int64_t,
          ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
          AccessPolicy Access = AccessPolicy::Readable, ResetPolicy Reset = ResetPolicy::Cumulative>
class Gauge {
    static_assert(std::is_same_v<ValueType, std::int64_t> || std::is_same_v<ValueType, double>,
                  "gauge values are std::int64_t or double");
    static_assert(std::is_trivially_copyable_v<ValueType>, "gauge values are trivially copyable");

public:
    /// @brief Metric kind discriminant reported in every snapshot.
    static constexpr MetricKind kind = MetricKind::Gauge;
    /// @brief Concurrency policy this instantiation was built with.
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    /// @brief Access policy controlling whether the read side is exposed.
    static constexpr AccessPolicy access = Access;
    /// @brief Reset policy controlling cumulative versus windowed reporting.
    static constexpr ResetPolicy reset = Reset;

    /// @brief Copy construction is deleted: instruments are registry-owned.
    Gauge(const Gauge&) = delete;
    /// @brief Copy assignment is deleted: instruments are registry-owned.
    Gauge& operator=(const Gauge&) = delete;
    /// @brief Move construction is deleted: instrument addresses are stable.
    Gauge(Gauge&&) = delete;
    /// @brief Move assignment is deleted: instrument addresses are stable.
    Gauge& operator=(Gauge&&) = delete;

    /// @brief Assign the current value.
    ///
    /// Under `MultiThreaded` this reassigns the sharded aggregate, so it is a
    /// bounded control-plane pass over the shards rather than a hot-path call.
    ///
    /// @param value New value for the aggregate, as observed at call time.
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge recording surface".
    void set(ValueType value) noexcept { storage_.set(value); }

    /// @brief Move the current value by a signed delta.
    ///
    /// @param delta Change added to the current value; it lands in exactly one
    ///        reset window.
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge recording surface".
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge reset windows".
    void add(ValueType delta) noexcept { storage_.add(delta); }

    /// @brief Move the current value down by a signed delta.
    ///
    /// @param delta Change subtracted from the current value, applied as
    ///        `-delta` on the same recording path as `add()`.
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge recording surface".
    void sub(ValueType delta) noexcept { storage_.add(static_cast<ValueType>(-delta)); }

    /// @brief Current value, or the window activity under `ResetOnRead`.
    ///
    /// Under `ResetPolicy::ResetOnRead` the call consumes the window: the
    /// reported value is returned once and the accumulator restarts at zero.
    /// Under `MultiThreaded` the read aggregates every shard.
    ///
    /// @return Current value, or the net delta since the previous observation.
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge reset windows".
    /// @note Available only when `Access == AccessPolicy::Readable` (spec
    ///       `metrics/metric-concepts`, requirement "Compile-time access
    ///       gating").
    [[nodiscard]] ValueType read() const
        requires(Access == AccessPolicy::Readable)
    {
        return storage_.take();
    }

    /// @brief Build the full `MetricRecord` for this instrument.
    ///
    /// Carries `MetricKind::Gauge`, the meter and metric identity, the policy
    /// classification and the current value; `is_double` records which union
    /// arm is populated. Under `ResetOnRead` it consumes the window exactly
    /// like `read()`.
    ///
    /// @return Record with classification filled in and the scalar payload set.
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge reset windows".
    /// @note Available only when `Access == AccessPolicy::Readable` (spec
    ///       `metrics/metric-concepts`, requirement "Compile-time access
    ///       gating").
    [[nodiscard]] MetricRecord snapshot() const
        requires(Access == AccessPolicy::Readable)
    {
        MetricRecord record = detail::make_record(kind, concurrency, access, reset, identity_);
        const ValueType value = storage_.take();
        if constexpr (std::is_same_v<ValueType, double>) {
            record.scalar.gauge_double = static_cast<double>(value);
            record.scalar.is_double = true;
        } else {
            record.scalar.gauge_int = static_cast<std::int64_t>(value);
            record.scalar.is_double = false;
        }
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
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge serialization
    ///       contract".
    [[nodiscard]] std::string to_string() const {
        std::string out = "Gauge(kind=";
        detail::append_classification(out, kind, concurrency, access, reset);
        out += ", value=" + std::to_string(storage_.observe());
        out += ")";
        return out;
    }

    /// @brief Serialize the gauge as diagnostic JSON.
    ///
    /// The object carries the classification fields plus `"value"`. It is
    /// deliberately write-only: there is no `from_json`, because an
    /// instrument can only be created by a `Meter`.
    ///
    /// @param j JSON value to assign to.
    /// @param gauge Gauge being serialized.
    /// @note Project mandate 10 (non-throwing `to_json` under
    ///       `JSON_NOEXCEPTION=1`).
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge serialization
    ///       contract".
    friend void to_json(nlohmann::json& j, const Gauge& gauge) {
        j = detail::classification_json(Gauge::kind, Gauge::concurrency, Gauge::access,
                                        Gauge::reset);
        j["value"] = gauge.storage_.observe();
    }

private:
    friend class Meter;
    friend class MetricProvider;

    /// @brief Create an empty gauge; the unnamed passkey proves `Meter` origin.
    ///
    /// The argument is intentionally unnamed: it only has to exist, and it can
    /// only be produced by `Meter`, so no other code path can construct an
    /// instrument.
    explicit Gauge(const MeterPasskey&) noexcept {}

    /// @brief Type-erased harvest hook used by the provider's plain C function pointer.
    ///
    /// @param object Instrument addressed as `void*` by the registry slot.
    /// @param record Record whose scalar payload receives the window value.
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge reset windows".
    static void harvest_thunk(void* object, MetricRecord& record) noexcept {
        const auto& self = *static_cast<const Gauge*>(object);
        const ValueType value = self.storage_.take();
        if constexpr (std::is_same_v<ValueType, double>) {
            record.scalar.gauge_double = static_cast<double>(value);
            record.scalar.is_double = true;
        } else {
            record.scalar.gauge_int = static_cast<std::int64_t>(value);
            record.scalar.is_double = false;
        }
    }

    /// @brief Identity of the owning registry slot, set by the `Meter` at creation.
    const MetricIdentity* identity_{nullptr};

    /// @brief Policy-selected storage; the only state of the instrument.
    detail::GaugeStorage<ValueType, Concurrency, Reset> storage_{};
};

static_assert(sizeof(Gauge<double, ConcurrencyPolicy::MultiThreaded, AccessPolicy::Readable,
                           ResetPolicy::ResetOnRead>) <= detail::max_gauge_size,
              "gauge must fit the registry budget");
static_assert(concepts::Metric<Gauge<std::int64_t, ConcurrencyPolicy::SingleThreaded,
                                     AccessPolicy::Readable, ResetPolicy::Cumulative>>,
              "Gauge must satisfy the Metric concept");
static_assert(concepts::ReadableMetric<Gauge<double, ConcurrencyPolicy::SingleThreaded,
                                             AccessPolicy::Readable, ResetPolicy::Cumulative>>,
              "a Readable gauge must satisfy the ReadableMetric concept");
static_assert(!concepts::ReadableMetric<Gauge<double, ConcurrencyPolicy::SingleThreaded,
                                              AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
              "a WriteOnly gauge must NOT satisfy the ReadableMetric concept");
static_assert(!detail::HasRead<Gauge<double, ConcurrencyPolicy::SingleThreaded,
                                     AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
              "a WriteOnly gauge must not expose read()");
static_assert(!detail::HasSnapshot<Gauge<double, ConcurrencyPolicy::SingleThreaded,
                                         AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
              "a WriteOnly gauge must not expose snapshot()");

/// @brief Free `to_string` helper mirroring `Gauge::to_string()`.
///
/// @tparam ValueType Stored value type of the gauge.
/// @tparam Concurrency Concurrency policy of the gauge.
/// @tparam Access Access policy of the gauge.
/// @tparam Reset Reset policy of the gauge.
/// @param gauge Gauge to render.
/// @return Diagnostic text, equal to `gauge.to_string()`.
/// @note Project mandate 9 (free `to_string` for every public type).
/// @note Spec `metrics/counter-gauge`, requirement "Gauge serialization
///       contract".
template <typename ValueType, nexigen::metrics::ConcurrencyPolicy Concurrency,
          nexigen::metrics::AccessPolicy Access, nexigen::metrics::ResetPolicy Reset>
[[nodiscard]] std::string
to_string(const nexigen::metrics::Gauge<ValueType, Concurrency, Access, Reset>& gauge) {
    return gauge.to_string();
}

} // namespace nexigen::metrics

/// @brief `std::format` support for `Gauge`, rendering its diagnostic string.
///
/// @tparam ValueType Stored value type of the formatted gauge.
/// @tparam Concurrency Concurrency policy of the formatted gauge.
/// @tparam Access Access policy of the formatted gauge.
/// @tparam Reset Reset policy of the formatted gauge.
/// @note Project mandate 11 (`std::formatter` for every public type).
/// @note Spec `metrics/counter-gauge`, requirement "Gauge serialization
///       contract".
template <typename ValueType, nexigen::metrics::ConcurrencyPolicy Concurrency,
          nexigen::metrics::AccessPolicy Access, nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::Gauge<ValueType, Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    /// @brief Format a gauge by delegating to its diagnostic string.
    ///
    /// @tparam Ctx Format-context type supplied by `std::format`.
    /// @param gauge Gauge to render.
    /// @param ctx Output context the text is written to.
    /// @return Iterator past the last character written.
    template <class Ctx>
    auto format(const nexigen::metrics::Gauge<ValueType, Concurrency, Access, Reset>& gauge,
                Ctx& ctx) const {
        const std::string text = gauge.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
