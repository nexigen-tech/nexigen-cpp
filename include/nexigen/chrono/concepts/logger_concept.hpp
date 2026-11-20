// nexigen::chrono — chrono-local worker logger concept (change
// add-chrono-clock-service; spec chrono/worker). Unified onto
// core::concepts::LevelLogger by change unify-core-concepts.
#pragma once

#include <nexigen/core/concepts/level_logger_concept.hpp>

namespace nexigen::chrono::concepts {

/// @brief The worker-side logger contract: one noexcept level/message call.
///
/// An alias of `core::concepts::LevelLogger`, so a worker can accept a logger
/// without depending on the logging module.
///
/// @tparam T Candidate logger type.
/// @note Spec `chrono/worker`, requirement "Worker logger binding".
template <class T>
concept Logger = core::concepts::LevelLogger<T>;

} // namespace nexigen::chrono::concepts
