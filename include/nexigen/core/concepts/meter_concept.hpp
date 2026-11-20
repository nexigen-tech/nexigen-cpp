// nexigen::core — meter and instrument concepts shared by telemetry modules
// (change unify-core-concepts; spec core/meter-concepts).
//
// The concepts name only std types and core::Result, so com, logging, tracing,
// and chrono can accept a meter without depending on nexigen::metrics. The
// instrument shapes are structural: value types and snapshot types are
// supplied by the implementation or constrained by the project's diagnostic
// mandate.
#pragma once

#include <concepts>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <nexigen/core/result.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::core::concepts {

/// @brief A type with a cold-path diagnostic rendering.
///
/// @tparam T Candidate type.
template <class T>
concept Diagnosable = requires(const T& value) {
    { value.to_string() } -> std::convertible_to<std::string>;
};

/// @brief Structural contract for one cumulative counter.
///
/// @tparam T Candidate counter type.
/// @note Spec `core/meter-concepts`, requirement "Counter concept".
template <class T>
concept CounterLike = requires(T& counter, std::uint64_t delta) {
    { counter.add(delta) } noexcept;
    { counter.inc() } noexcept;
};

/// @brief Structural contract for a numeric gauge.
///
/// A gauge sets, adds, and subtracts its value. Reading is intentionally not
/// required: the metrics module supports write-only instruments.
///
/// @tparam G Candidate gauge type.
/// @tparam Value Value type the gauge stores.
/// @note Spec `core/meter-concepts`, requirement "Gauge concept".
template <class G, class Value>
concept GaugeLike = requires(G& gauge, Value value) {
    { gauge.set(value) } noexcept;
    { gauge.add(value) } noexcept;
    { gauge.sub(value) } noexcept;
};

/// @brief Structural contract for a named-state gauge.
///
/// @tparam S Candidate state-gauge type.
/// @note Spec `core/meter-concepts`, requirement "State gauge concept".
template <class S>
concept StateGaugeLike = requires(S& gauge, std::string_view state) {
    { gauge.set(state) } noexcept -> std::same_as<bool>;
};

/// @brief Structural contract for descriptive statistics.
///
/// @tparam D Candidate statistics instrument.
/// @note Spec `core/meter-concepts`, requirement "Descriptive statistics
///       concept".
template <class D>
concept DescriptiveStatisticsLike = requires(D& stats, double value) {
    { stats.record(value) } noexcept;
    { stats.read() } -> Diagnosable;
    { stats.snapshot() } -> Diagnosable;
};

/// @brief Structural contract for a histogram.
///
/// Shared by explicit-bucket and exponential histograms.
///
/// @tparam H Candidate histogram type.
/// @note Spec `core/meter-concepts`, requirement "Histogram concept".
template <class H>
concept HistogramLike = requires(H& histogram, double value) {
    { histogram.record(value) } noexcept;
    { histogram.read() } -> Diagnosable;
    { histogram.snapshot() } -> Diagnosable;
};

/// @brief Structural contract for a counter factory (a "meter").
///
/// `create_counter(name, unit)` returns a `core::Result`-like value holding a
/// pointer to a `CounterLike`.
///
/// @tparam M Candidate meter type.
/// @note Spec `core/meter-concepts`, requirement "Meter concept".
template <class M>
concept MeterLike = requires(M& meter, std::string_view name, std::string_view unit) {
    { **meter.template create_counter<>(name, unit) } -> CounterLike;
};

/// @brief Full factory contract: every instrument kind the metrics module owns.
///
/// Stronger than `MeterLike`; used where a module needs more than counters.
///
/// @tparam M Candidate meter type.
/// @note Spec `core/meter-concepts`, requirement "Instrument meter concept".
template <class M>
concept InstrumentMeterLike = MeterLike<M> && requires(M& meter, std::string_view name,
                                                       std::span<const double> bounds,
                                                       std::int32_t scale) {
    requires GaugeLike<std::remove_cvref_t<decltype(**meter.template create_gauge<>(name))>,
                       std::int64_t>;
    requires StateGaugeLike<std::remove_cvref_t<decltype(**meter.template create_state<>(name))>>;
    requires DescriptiveStatisticsLike<
        std::remove_cvref_t<decltype(**meter.template create_statistics<>(name))>>;
    requires HistogramLike<
        std::remove_cvref_t<decltype(**meter.template create_explicit_histogram<>(name, bounds))>>;
    requires HistogramLike<std::remove_cvref_t<
        decltype(**meter.template create_exponential_histogram<>(name, scale))>>;
};

} // namespace nexigen::core::concepts
