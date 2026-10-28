// nexigen::metrics — compile-time probes (design D5).
//
// The read side of a WriteOnly instrument must be absent at compile time.
// A requires-expression that is not dependent is checked eagerly and an
// invalid expression is a hard error, so the probes live in concepts: their
// bodies are only substituted when the concept is evaluated, and a
// substitution failure simply makes the concept false.
#pragma once

namespace nexigen::metrics::detail {

/// True when T exposes a public read().
template <class T>
concept HasRead = requires(const T& value) { value.read(); };

/// True when T exposes a public snapshot().
template <class T>
concept HasSnapshot = requires(const T& value) { value.snapshot(); };

} // namespace nexigen::metrics::detail
