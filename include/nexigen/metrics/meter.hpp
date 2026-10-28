// nexigen::metrics — meter handles and the instrument factory (design D5/D6;
// spec metrics/metric-provider "Factory hierarchy and passkey construction").
//
// A Meter is the ONLY way to create instruments, and a Meter itself is only
// obtainable from MetricProvider::get_meter(name, version). Instrument
// constructors are private and take a MeterPasskey — a tag only a Meter can
// create — so user code cannot build a Counter, Gauge, DescriptiveStatistics,
// ExplicitBucketHistogram or ExponentialHistogram directly (pinned by
// static_assert(!std::is_constructible_v<...>) in the test TU).
//
// Creation returns core::Result<T*> — a raw pointer into the provider's
// pre-allocated registry. The pointer stays valid until the provider is
// destroyed.
//
// The method bodies live in metric_provider.hpp: they need the complete
// MetricProvider type to reserve a registry slot.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/counter.hpp>
#include <nexigen/metrics/descriptive_statistics.hpp>
#include <nexigen/metrics/exponential_histogram.hpp>
#include <nexigen/metrics/explicit_histogram.hpp>
#include <nexigen/metrics/gauge.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

class MetricProvider;

/// Construction guard: only a Meter may create instruments.
class MeterPasskey {
    MeterPasskey() = default;
    friend class Meter;

public:
    /// Cold-path diagnostic (mandate 9). The tag carries no state.
    [[nodiscard]] std::string to_string() const { return "MeterPasskey"; }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const MeterPasskey&) { return "MeterPasskey"; }

/// A named factory for instruments. Obtain one from MetricProvider::get_meter.
class Meter {
public:
    Meter(const Meter&) = delete;
    Meter& operator=(const Meter&) = delete;
    Meter(Meter&&) = delete;
    Meter& operator=(Meter&&) = delete;

    /// The meter identity reported by every harvested record.
    [[nodiscard]] const MetricIdentity& identity() const noexcept { return identity_; }

    /// Create a counter.
    template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
              AccessPolicy Access = AccessPolicy::Readable,
              ResetPolicy Reset = ResetPolicy::Cumulative>
    [[nodiscard]] core::Result<Counter<Concurrency, Access, Reset>*>
    create_counter(std::string_view name);

    /// Create a gauge.
    template <typename ValueType = std::int64_t,
              ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
              AccessPolicy Access = AccessPolicy::Readable,
              ResetPolicy Reset = ResetPolicy::Cumulative>
    [[nodiscard]] core::Result<Gauge<ValueType, Concurrency, Access, Reset>*>
    create_gauge(std::string_view name);

    /// Create a descriptive-statistics instrument.
    template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
              AccessPolicy Access = AccessPolicy::Readable,
              ResetPolicy Reset = ResetPolicy::Cumulative>
    [[nodiscard]] core::Result<DescriptiveStatistics<Concurrency, Access, Reset>*>
    create_statistics(std::string_view name);

    /// Create an explicit-bucket histogram. The bounds must be finite and
    /// strictly increasing; an invalid set is rejected with InvalidArgument.
    template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
              AccessPolicy Access = AccessPolicy::Readable,
              ResetPolicy Reset = ResetPolicy::Cumulative>
    [[nodiscard]] core::Result<ExplicitBucketHistogram<Concurrency, Access, Reset>*>
    create_explicit_histogram(std::string_view name, std::span<const double> bounds);

    /// Create an exponential histogram. The scale must be within 0..20.
    template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
              AccessPolicy Access = AccessPolicy::Readable,
              ResetPolicy Reset = ResetPolicy::Cumulative>
    [[nodiscard]] core::Result<ExponentialHistogram<Concurrency, Access, Reset>*>
    create_exponential_histogram(std::string_view name, std::int32_t scale);

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "Meter(name=" + std::string(detail::view_name(identity_.meter_name)) +
               ", version=" + std::string(detail::view_name(identity_.meter_version)) + ")";
    }

private:
    friend class MetricProvider;

    Meter() = default;

    MetricProvider* provider_{nullptr};
    MetricIdentity identity_{};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const Meter& meter) { return meter.to_string(); }

} // namespace nexigen::metrics

/// The passkey tag formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::MeterPasskey> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::MeterPasskey& key, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(std::string_view("MeterPasskey"), ctx);
    }
};
