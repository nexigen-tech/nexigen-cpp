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

/// @brief Structural contract for every published metrics instrument.
///
/// `Metric` is satisfied by a type that exposes, without virtual functions
/// and without RTTI, four compile-time policy constants and one cold-path
/// diagnostic renderer:
/// - `static constexpr MetricKind kind`;
/// - `static constexpr ConcurrencyPolicy concurrency`;
/// - `static constexpr AccessPolicy access`;
/// - `static constexpr ResetPolicy reset`;
/// - `std::string to_string() const`, callable on a `const T&` and
///   convertible to `std::string`.
///
/// The concept costs nothing at runtime: it is used only in constant
/// expressions and `static_assert`s, never as a check on the recording path.
///
/// @tparam T Instrument type under test. Every policy member must be
///         convertible to its enum type, so an integer-valued `kind` fails
///         the concept; a missing policy member or `to_string()` also fails
///         it, at compile time.
/// @note Spec `metrics/metric-concepts`, requirement "Metric concept core
///       surface".
template <class T>
concept Metric = requires(const T& value) {
    { T::kind } -> std::convertible_to<MetricKind>;
    { T::concurrency } -> std::convertible_to<ConcurrencyPolicy>;
    { T::access } -> std::convertible_to<AccessPolicy>;
    { T::reset } -> std::convertible_to<ResetPolicy>;
    { value.to_string() } -> std::convertible_to<std::string>;
};

/// @brief Refinement of `Metric` for instruments with a public read side.
///
/// Adds the two const-callable operations published by instruments built
/// with `AccessPolicy::Readable`: `read()`, returning the instrument's
/// current value summary (the concrete type is instrument-specific), and
/// `snapshot()`, returning a full `MetricRecord` convertible to that type.
/// Both are allocation-free and non-blocking. A `WriteOnly` instrument fails
/// this concept, and the same `requires` constraint is what removes those
/// members from its surface.
///
/// @tparam T Instrument type under test. It must first satisfy `Metric`,
///         then expose a const-callable `read()` and a const-callable
///         `snapshot()` convertible to `MetricRecord`.
/// @note Spec `metrics/metric-concepts`, requirement "Readable metric
///       concept".
/// @note Spec `metrics/metric-concepts`, requirement "Compile-time access
///       gating".
template <class T>
concept ReadableMetric = Metric<T> && requires(const T& value) {
    { value.read() };
    { value.snapshot() } -> std::convertible_to<MetricRecord>;
};

} // namespace nexigen::metrics::concepts
