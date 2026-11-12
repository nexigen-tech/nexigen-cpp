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
#include <nexigen/metrics/state_gauge.hpp>

namespace nexigen::metrics {

class MetricProvider;

/// @brief Construction guard: only a `Meter` may create instruments.
///
/// Its only constructor is private and `Meter` is its friend, so passing one by
/// reference to an instrument constructor proves factory authority. It carries
/// no state and adds no runtime cost.
///
/// @note Spec `metrics/metric-provider`, requirement "Factory hierarchy and
///       passkey construction".
/// @note Spec `metrics/metric-provider`, requirement "Meter instrument
///       creation".
class MeterPasskey {
    /// @brief Private default constructor: only `Meter` is a friend.
    MeterPasskey() = default;
    friend class Meter;

public:
    /// @brief Cold-path diagnostic string (project mandate 9).
    ///
    /// @return The literal `"MeterPasskey"`; the tag carries no state.
    [[nodiscard]] std::string to_string() const { return "MeterPasskey"; }
};

/// @brief Free `to_string` overload mirroring the member (project mandate 9).
///
/// @return The literal `"MeterPasskey"`.
[[nodiscard]] inline std::string to_string(const MeterPasskey&) {
    return "MeterPasskey";
}

/// @brief A named factory for instruments, owned by a `MetricProvider`.
///
/// A meter is the only object that can construct instruments: it passes a
/// `MeterPasskey` to the private instrument constructors and reserves the
/// registry slot on the caller's behalf. Obtain one from
/// `MetricProvider::get_meter(name, version)`; meters are non-copyable and
/// non-movable and must not outlive their provider.
///
/// Creation is cold-path and returns `core::Result<T*>`, never throwing. The
/// pointed-to instrument stays at a fixed registry address until the provider is
/// destroyed. Recording on the returned instrument is the only hot path.
///
/// @note Spec `metrics/metric-provider`, requirement "Factory hierarchy and
///       passkey construction".
/// @note Spec `metrics/metric-provider`, requirement "Meter instrument
///       creation".
/// @warning Meters are provider-owned handles: using one after its provider is
///          destroyed is a lifetime violation.
class Meter {
public:
    /// @brief Deleted: meters are provider-owned handles, not values.
    Meter(const Meter&) = delete;
    /// @brief Deleted: meters are provider-owned handles, not values.
    Meter& operator=(const Meter&) = delete;
    /// @brief Deleted: meters live at a fixed provider address.
    Meter(Meter&&) = delete;
    /// @brief Deleted: meters live at a fixed provider address.
    Meter& operator=(Meter&&) = delete;

    /// @brief Identity reported by every record harvested from this meter.
    ///
    /// @return The meter name/version pair copied at creation, valid for the
    ///         meter's lifetime.
    [[nodiscard]] const MetricIdentity& identity() const noexcept { return identity_; }

    /// @brief Create a counter registered under this meter.
    ///
    /// Reserves a registry slot, placement-constructs the counter with the
    /// requested policies and wires its identity. Cold path.
    ///
    /// @tparam Concurrency Recording policy; defaults to `SingleThreaded`.
    /// @tparam Access `Readable` or `WriteOnly`; defaults to `Readable`.
    /// @tparam Reset `Cumulative` or `ResetOnRead`; defaults to `Cumulative`.
    /// @param name Metric name; must be non-empty and fit `max_name_length`.
    /// @param unit Optional unit string reported with every record.
    /// @return Pointer to the registry-owned counter on success. The error arm
    ///         is `core::ErrorCode::InvalidArgument` for an empty, over-long or
    ///         duplicate name, and `core::ErrorCode::BufferFull` when the
    ///         counter pool is exhausted.
    /// @note Spec `metrics/metric-provider`, requirement "Meter instrument
    ///       creation".
    /// @note Spec `metrics/metric-provider`, requirement "Pre-allocated aligned
    ///       metric registry".
    template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
              AccessPolicy Access = AccessPolicy::Readable,
              ResetPolicy Reset = ResetPolicy::Cumulative>
    [[nodiscard]] core::Result<Counter<Concurrency, Access, Reset>*>
    create_counter(std::string_view name, std::string_view unit = {});

    /// @brief Create a gauge registered under this meter.
    ///
    /// Reserves a registry slot, placement-constructs the gauge with the
    /// requested value type and policies and wires its identity. Cold path.
    ///
    /// @tparam ValueType Numeric, trivially copyable value type stored by the
    ///         gauge; defaults to `std::int64_t`.
    /// @tparam Concurrency Recording policy; defaults to `SingleThreaded`.
    /// @tparam Access `Readable` or `WriteOnly`; defaults to `Readable`.
    /// @tparam Reset `Cumulative` or `ResetOnRead`; defaults to `Cumulative`.
    /// @param name Metric name; must be non-empty and fit `max_name_length`.
    /// @param unit Optional unit string reported with every record.
    /// @return Pointer to the registry-owned gauge on success. The error arm is
    ///         `core::ErrorCode::InvalidArgument` for an invalid or duplicate
    ///         name, and `core::ErrorCode::BufferFull` when the gauge pool is
    ///         exhausted.
    /// @note Spec `metrics/metric-provider`, requirement "Meter instrument
    ///       creation".
    template <typename ValueType = std::int64_t,
              ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
              AccessPolicy Access = AccessPolicy::Readable,
              ResetPolicy Reset = ResetPolicy::Cumulative>
    [[nodiscard]] core::Result<Gauge<ValueType, Concurrency, Access, Reset>*>
    create_gauge(std::string_view name, std::string_view unit = {});

    /// @brief Create a string-valued state gauge registered under this meter.
    ///
    /// A state gauge reports the current named state rather than a numeric
    /// value. Reserves a registry slot and wires the identity. Cold path.
    ///
    /// @tparam Concurrency Recording policy; defaults to `SingleThreaded`.
    /// @tparam Access `Readable` or `WriteOnly`; defaults to `Readable`.
    /// @tparam Reset `Cumulative` or `ResetOnRead`; defaults to `Cumulative`.
    /// @param name Metric name; must be non-empty and fit `max_name_length`.
    /// @param unit Optional unit string reported with every record.
    /// @return Pointer to the registry-owned state gauge on success. The error
    ///         arm is `core::ErrorCode::InvalidArgument` for an invalid or
    ///         duplicate name, and `core::ErrorCode::BufferFull` when the state
    ///         pool is exhausted.
    /// @note Spec `metrics/metric-provider`, requirement "Meter instrument
    ///       creation".
    template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
              AccessPolicy Access = AccessPolicy::Readable,
              ResetPolicy Reset = ResetPolicy::Cumulative>
    [[nodiscard]] core::Result<StateGauge<Concurrency, Access, Reset>*>
    create_state(std::string_view name, std::string_view unit = {});

    /// @brief Create a descriptive-statistics instrument under this meter.
    ///
    /// Reserves a registry slot, placement-constructs the instrument with the
    /// requested policies and wires its identity. Cold path.
    ///
    /// @tparam Concurrency Recording policy; defaults to `SingleThreaded`.
    /// @tparam Access `Readable` or `WriteOnly`; defaults to `Readable`.
    /// @tparam Reset `Cumulative` or `ResetOnRead`; defaults to `Cumulative`.
    /// @param name Metric name; must be non-empty and fit `max_name_length`.
    /// @param unit Optional unit string reported with every record.
    /// @return Pointer to the registry-owned instrument on success. The error
    ///         arm is `core::ErrorCode::InvalidArgument` for an invalid or
    ///         duplicate name, and `core::ErrorCode::BufferFull` when the
    ///         statistics pool is exhausted.
    /// @note Spec `metrics/metric-provider`, requirement "Meter instrument
    ///       creation".
    template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
              AccessPolicy Access = AccessPolicy::Readable,
              ResetPolicy Reset = ResetPolicy::Cumulative>
    [[nodiscard]] core::Result<DescriptiveStatistics<Concurrency, Access, Reset>*>
    create_statistics(std::string_view name, std::string_view unit = {});

    /// @brief Create an explicit-bucket histogram registered under this meter.
    ///
    /// Validates the bounds before touching the registry, then reserves a slot,
    /// constructs the instrument and installs a copy of the bounds. Cold path.
    ///
    /// @tparam Concurrency Recording policy; defaults to `SingleThreaded`.
    /// @tparam Access `Readable` or `WriteOnly`; defaults to `Readable`.
    /// @tparam Reset `Cumulative` or `ResetOnRead`; defaults to `Cumulative`.
    /// @param name Metric name; must be non-empty and fit `max_name_length`.
    /// @param bounds Finite, strictly increasing upper bounds; at least one and
    ///        at most `detail::max_explicit_bounds`.
    /// @param unit Optional unit string reported with every record.
    /// @return Pointer to the registry-owned histogram on success. The error arm
    ///         is `core::ErrorCode::InvalidArgument` for empty, non-finite,
    ///         unsorted or too many bounds, an invalid or duplicate name, and
    ///         `core::ErrorCode::BufferFull` when the histogram pool is
    ///         exhausted.
    /// @note Spec `metrics/metric-provider`, requirement "Meter instrument
    ///       creation".
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit bucket
    ///       configuration".
    template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
              AccessPolicy Access = AccessPolicy::Readable,
              ResetPolicy Reset = ResetPolicy::Cumulative>
    [[nodiscard]] core::Result<ExplicitBucketHistogram<Concurrency, Access, Reset>*>
    create_explicit_histogram(std::string_view name, std::span<const double> bounds,
                              std::string_view unit = {});

    /// @brief Create an exponential histogram registered under this meter.
    ///
    /// Validates the scale before touching the registry, then reserves a slot and
    /// constructs the instrument with that finest resolution. Cold path.
    ///
    /// @tparam Concurrency Recording policy; defaults to `SingleThreaded`.
    /// @tparam Access `Readable` or `WriteOnly`; defaults to `Readable`.
    /// @tparam Reset `Cumulative` or `ResetOnRead`; defaults to `Cumulative`.
    /// @param name Metric name; must be non-empty and fit `max_name_length`.
    /// @param scale Configured scale in `[0, 20]`; larger values are finer.
    /// @param unit Optional unit string reported with every record.
    /// @return Pointer to the registry-owned histogram on success. The error arm
    ///         is `core::ErrorCode::InvalidArgument` for an out-of-range scale or
    ///         an invalid or duplicate name, and `core::ErrorCode::BufferFull`
    ///         when the histogram pool is exhausted.
    /// @note Spec `metrics/exponential-histogram`, requirement "Exponential scale
    ///       configuration".
    /// @note Spec `metrics/metric-provider`, requirement "Meter instrument
    ///       creation".
    template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
              AccessPolicy Access = AccessPolicy::Readable,
              ResetPolicy Reset = ResetPolicy::Cumulative>
    [[nodiscard]] core::Result<ExponentialHistogram<Concurrency, Access, Reset>*>
    create_exponential_histogram(std::string_view name, std::int32_t scale,
                                 std::string_view unit = {});

    /// @brief Cold-path diagnostic string naming the meter and version.
    ///
    /// @return Implementation-defined text; the free `to_string` mirrors it.
    /// @note Project mandate 9: every public type renders `to_string()`.
    [[nodiscard]] std::string to_string() const {
        return "Meter(name=" + std::string(detail::view_name(identity_.meter_name)) +
               ", version=" + std::string(detail::view_name(identity_.meter_version)) + ")";
    }

private:
    friend class MetricProvider;

    /// @brief Shared factory prologue: reserve, construct, identify.
    ///
    /// Locks the provider's harvest mutex, resolves the meter identity, copies
    /// the metric name, reserves a registry slot, placement-constructs the
    /// instrument via `construct` (which also runs instrument-specific init such
    /// as `install_bounds`), and wires the instrument's identity pointer.
    ///
    /// @tparam Instrument Concrete instrument type being created.
    /// @tparam Construct Callable taking `void*` and returning `Instrument*`.
    /// @param kind Registry kind selecting the slot table and arena.
    /// @param name Metric name; must be non-empty and fit the name buffer.
    /// @param unit Optional unit string; an over-long unit is rejected.
    /// @param concurrency Recording policy recorded in the slot metadata.
    /// @param access Access policy recorded in the slot metadata.
    /// @param reset Reset policy recorded in the slot metadata.
    /// @param construct Placement-construction callable invoked on the slot.
    /// @return Pointer to the constructed instrument on success; the error arm
    ///         is `InvalidArgument` for bad names or `BufferFull` when the kind's
    ///         pool is exhausted.
    /// @warning The harvest mutex serializes publication with harvesting; this is
    ///          cold path only and never touched by instrument recording.
    template <class Instrument, class Construct>
    core::Result<Instrument*> create_instrument(MetricKind kind, std::string_view name,
                                                std::string_view unit,
                                                ConcurrencyPolicy concurrency, AccessPolicy access,
                                                ResetPolicy reset, Construct&& construct);

    /// @brief Private constructor: only `MetricProvider` may create meters.
    Meter() = default;

    /// @brief Provider that owns this meter and its registry slot.
    MetricProvider* provider_{nullptr};
    /// @brief Name/version identity harvested with every record.
    MetricIdentity identity_{};
};

/// @brief Free `to_string` overload mirroring the member (project mandate 9).
///
/// @param meter Meter to render.
/// @return The same diagnostic text as `meter.to_string()`.
[[nodiscard]] inline std::string to_string(const Meter& meter) {
    return meter.to_string();
}

} // namespace nexigen::metrics

/// @brief `std::formatter` rendering the passkey tag as `"MeterPasskey"`.
///
/// @note Project mandate 11: `std::formatter` support for diagnostics.
/// @note Spec `metrics/metric-provider`, requirement "Factory hierarchy and
///       passkey construction".
template <>
struct std::formatter<nexigen::metrics::MeterPasskey> : std::formatter<std::string_view> {
    /// @brief Format the tag as its fixed diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param key Passkey tag to render; ignored because the tag is stateless.
    /// @param ctx Output context to append the text to.
    /// @return Iterator past the last character written.
    template <class Ctx>
    auto format([[maybe_unused]] const nexigen::metrics::MeterPasskey& key, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(std::string_view("MeterPasskey"), ctx);
    }
};
