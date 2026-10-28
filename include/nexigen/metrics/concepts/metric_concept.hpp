// nexigen::metrics — structural concepts for instruments (design D5;
// spec metrics/metric-concepts).
//
// Metric        — every published instrument: static policy metadata plus a
//                 cold-path diagnostic string. No virtual functions, no RTTI.
// ReadableMetric— the refinement for AccessPolicy::Readable instruments: a
//                 const-callable read() (current value summary) and
//                 snapshot() (full MetricRecord).
//
// The read side of a WriteOnly instrument is gated at compile time by the
// member constraint `requires (Access == AccessPolicy::Readable)`, so the
// concept refinement and the call-site rejection are the same mechanism.
#pragma once

#include <concepts>
#include <string>

#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics::concepts {

/// A metrics instrument: policy metadata + diagnostic string.
template <class T>
concept Metric = requires(const T& value) {
    { T::kind } -> std::convertible_to<MetricKind>;
    { T::concurrency } -> std::convertible_to<ConcurrencyPolicy>;
    { T::access } -> std::convertible_to<AccessPolicy>;
    { T::reset } -> std::convertible_to<ResetPolicy>;
    { value.to_string() } -> std::convertible_to<std::string>;
};

/// A readable instrument: adds the public read side.
template <class T>
concept ReadableMetric = Metric<T> && requires(const T& value) {
    { value.read() };
    { value.snapshot() } -> std::convertible_to<MetricRecord>;
};

} // namespace nexigen::metrics::concepts
