// nexigen::metrics — compile-time probes (design D5).
//
// The read side of a WriteOnly instrument must be absent at compile time.
// A requires-expression that is not dependent is checked eagerly and an
// invalid expression is a hard error, so the probes live in concepts: their
// bodies are only substituted when the concept is evaluated, and a
// substitution failure simply makes the concept false.
#pragma once

namespace nexigen::metrics::detail {

/// @brief True when `T` exposes a public, const-callable `read()`.
///
/// Lives in a concept so the probe is only substituted when the concept is
/// evaluated; an eager non-dependent `requires` would make a `WriteOnly`
/// instrument a hard error instead of a `false` result.
///
/// @tparam T Type probed for the read surface.
/// @note Spec `metrics/metric-concepts`, requirement "Compile-time access
///       gating".
template <class T>
concept HasRead = requires(const T& value) { value.read(); };

/// @brief True when `T` exposes a public, const-callable `snapshot()`.
///
/// Same substitution rationale as `HasRead`: the probe must fail softly so that
/// `ReadableMetric` can refine `Metric` without breaking `WriteOnly` types.
///
/// @tparam T Type probed for the snapshot surface.
/// @note Spec `metrics/metric-concepts`, requirement "Compile-time access
///       gating".
template <class T>
concept HasSnapshot = requires(const T& value) { value.snapshot(); };

} // namespace nexigen::metrics::detail
