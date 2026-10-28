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
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/detail/probes.hpp>
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

/// Multi-threaded gauge shard: 64-byte aligned atomic cell.
template <typename ValueType>
struct alignas(64) Gauge_shard {
    std::atomic<ValueType> value{0};
};

/// Gauge storage selected by the concurrency policy.
template <typename ValueType, ConcurrencyPolicy Concurrency, ResetPolicy Reset>
class Gauge_storage;

template <typename ValueType, ResetPolicy Reset>
class Gauge_storage<ValueType, ConcurrencyPolicy::SingleThreaded, Reset> {
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
class Gauge_storage<ValueType, ConcurrencyPolicy::MultiThreaded, Reset> {
public:
    void set(ValueType value) noexcept {
        // Move the aggregate to `value`: apply the difference to the caller's
        // own shard (a control-plane operation — one pass over the shards).
        const ValueType delta = static_cast<ValueType>(value - aggregate());
        shards_[detail::thread_slot(MetricKind::Gauge)].value.fetch_add(delta, std::memory_order_relaxed);
    }

    void add(ValueType delta) noexcept {
        shards_[detail::thread_slot(MetricKind::Gauge)].value.fetch_add(delta, std::memory_order_relaxed);
    }

    [[nodiscard]] ValueType aggregate() const noexcept {
        ValueType total{};
        for (const auto& shard : shards_) {
            total = static_cast<ValueType>(total + shard.value.load(std::memory_order_relaxed));
        }
        return total;
    }

    [[nodiscard]] ValueType observe() const noexcept { return aggregate(); }

    [[nodiscard]] ValueType take() const noexcept {
        ValueType total{};
        for (auto& shard : shards_) {
            if constexpr (Reset == ResetPolicy::ResetOnRead) {
                total = static_cast<ValueType>(
                    total + shard.value.exchange(ValueType{0}, std::memory_order_relaxed));
            } else {
                total = static_cast<ValueType>(total + shard.value.load(std::memory_order_relaxed));
            }
        }
        return total;
    }

private:
    mutable Gauge_shard<ValueType> shards_[shard_count]{};
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
        MetricRecord record{};
        record.kind = kind;
        record.concurrency = concurrency;
        record.access = access;
        record.reset = reset;
        const ValueType value = storage_.take();
        if constexpr (std::is_same_v<ValueType, double>) {
            record.scalar.gauge_double = static_cast<double>(value);
            record.scalar.is_double = true;
        } else {
            record.scalar.gauge_int = static_cast<std::int64_t>(value);
            record.scalar.is_double = false;
        }
        if (identity_ != nullptr) {
            record.identity = *identity_;
        }
        return record;
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        std::string out = "Gauge(kind=";
        out += std::string(metrics::to_string(kind));
        out += ", concurrency=" + std::string(metrics::to_string(concurrency));
        out += ", access=" + std::string(metrics::to_string(access));
        out += ", reset=" + std::string(metrics::to_string(reset));
        out += ", value=" + std::to_string(storage_.observe());
        out += ")";
        return out;
    }

    /// Diagnostic JSON (cold path). The instrument is NOT deserializable: its
    /// construction is Meter-only.
    friend void to_json(nlohmann::json& j, const Gauge& gauge) {
        j = nlohmann::json{
            {"kind", std::string(metrics::to_string(Gauge::kind))},
            {"concurrency", std::string(metrics::to_string(Gauge::concurrency))},
            {"access", std::string(metrics::to_string(Gauge::access))},
            {"reset", std::string(metrics::to_string(Gauge::reset))},
            {"value", gauge.storage_.observe()},
        };
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

    static void destroy_thunk(void* object) noexcept {
        static_cast<Gauge*>(object)->~Gauge();
    }

    /// Identity of the owning registry slot (set by the Meter at creation).
    const MetricIdentity* identity_{nullptr};

    detail::Gauge_storage<ValueType, Concurrency, Reset> storage_{};
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
