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

template <typename ValueType, nexigen::metrics::ConcurrencyPolicy Concurrency,
          nexigen::metrics::AccessPolicy Access, nexigen::metrics::ResetPolicy Reset>
class Gauge;

/// Construction guard: only a Meter may create instruments.
class MeterPasskey;

namespace detail {

/// Gauge storage selected by the concurrency policy.
template <typename ValueType, ConcurrencyPolicy Concurrency, ResetPolicy Reset>
class GaugeStorage;

template <typename ValueType, ResetPolicy Reset>
class GaugeStorage<ValueType, ConcurrencyPolicy::SingleThreaded, Reset> {
public:
    void set(ValueType value) noexcept { cell_ = value; }
    void add(ValueType delta) noexcept { cell_ = static_cast<ValueType>(cell_ + delta); }

    [[nodiscard]] ValueType observe() const noexcept { return cell_; }

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
    mutable ValueType cell_{};
};

template <typename ValueType, ResetPolicy Reset>
class GaugeStorage<ValueType, ConcurrencyPolicy::MultiThreaded, Reset>
    : public detail::ShardedScalar<ValueType, MetricKind::Gauge, Reset> {
public:
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

/// A current-value instrument. Constructible only through a Meter.
template <typename ValueType = std::int64_t,
          ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
          AccessPolicy Access = AccessPolicy::Readable,
          ResetPolicy Reset = ResetPolicy::Cumulative>
class Gauge {
    static_assert(std::is_same_v<ValueType, std::int64_t> || std::is_same_v<ValueType, double>,
                  "gauge values are std::int64_t or double");
    static_assert(std::is_trivially_copyable_v<ValueType>, "gauge values are trivially copyable");

public:
    static constexpr MetricKind kind = MetricKind::Gauge;
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    static constexpr AccessPolicy access = Access;
    static constexpr ResetPolicy reset = Reset;

    Gauge(const Gauge&) = delete;
    Gauge& operator=(const Gauge&) = delete;
    Gauge(Gauge&&) = delete;
    Gauge& operator=(Gauge&&) = delete;

    /// Assign the current value.
    void set(ValueType value) noexcept { storage_.set(value); }

    /// Move the current value by a delta.
    void add(ValueType delta) noexcept { storage_.add(delta); }

    /// Move the current value down by a delta.
    void sub(ValueType delta) noexcept {
        storage_.add(static_cast<ValueType>(-delta));
    }

    /// Current value (or the window delta for ResetOnRead — which also clears
    /// the window). Available only for Readable instruments.
    [[nodiscard]] ValueType read() const requires (Access == AccessPolicy::Readable) {
        return storage_.take();
    }

    /// Full record for this instrument (identity, classification, values).
    [[nodiscard]] MetricRecord snapshot() const requires (Access == AccessPolicy::Readable) {
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

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        std::string out = "Gauge(kind=";
        detail::append_classification(out, kind, concurrency, access, reset);
        out += ", value=" + std::to_string(storage_.observe());
        out += ")";
        return out;
    }

    /// Diagnostic JSON (cold path). The instrument is NOT deserializable: its
    /// construction is Meter-only.
    friend void to_json(nlohmann::json& j, const Gauge& gauge) {
        j = detail::classification_json(Gauge::kind, Gauge::concurrency, Gauge::access,
                                        Gauge::reset);
        j["value"] = gauge.storage_.observe();
    }

private:
    friend class Meter;
    friend class MetricProvider;

    explicit Gauge(const MeterPasskey&) noexcept {}

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

    /// Identity of the owning registry slot (set by the Meter at creation).
    const MetricIdentity* identity_{nullptr};

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
                                                AccessPolicy::WriteOnly,
                                                ResetPolicy::Cumulative>>,
              "a WriteOnly gauge must NOT satisfy the ReadableMetric concept");
static_assert(!detail::HasRead<Gauge<double, ConcurrencyPolicy::SingleThreaded,
                                      AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
              "a WriteOnly gauge must not expose read()");
static_assert(!detail::HasSnapshot<Gauge<double, ConcurrencyPolicy::SingleThreaded,
                                         AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
              "a WriteOnly gauge must not expose snapshot()");

/// Free helper (mandate 9): mirrors the member.
template <typename ValueType, nexigen::metrics::ConcurrencyPolicy Concurrency,
          nexigen::metrics::AccessPolicy Access, nexigen::metrics::ResetPolicy Reset>
[[nodiscard]] std::string to_string(
    const nexigen::metrics::Gauge<ValueType, Concurrency, Access, Reset>& gauge) {
    return gauge.to_string();
}

} // namespace nexigen::metrics


/// Gauges format as their diagnostic string (mandate 11).
template <typename ValueType, nexigen::metrics::ConcurrencyPolicy Concurrency,
          nexigen::metrics::AccessPolicy Access, nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::Gauge<ValueType, Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::Gauge<ValueType, Concurrency, Access, Reset>& gauge,
                Ctx& ctx) const {
        const std::string text = gauge.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
