// nexigen::logging — the Logger contract (design D3; unified onto
// core::concepts::Logger by change unify-core-concepts).
//
// The shape lives in core; logging supplies its format wrapper so the concept
// checks the real variadic signatures.
#pragma once

#include <nexigen/core/concepts/logger_concept.hpp>
#include <nexigen/logging/format_with_location.hpp>

namespace nexigen::logging::concepts {

/// @brief Structural C++23 contract every `nexigen::logging` sink satisfies.
///
/// An alias of `core::concepts::Logger<T, FormatWithLocation>`: runtime
/// threshold control, the explicit-level variadic entry, the six severity
/// conveniences, and the control-plane `flush()` barrier.
///
/// @tparam T Type under test.
/// @note Spec `logging/logger-concept`, requirement "Logger concept core
///       surface".
template <class T>
concept Logger = core::concepts::Logger<T, FormatWithLocation>;

} // namespace nexigen::logging::concepts
