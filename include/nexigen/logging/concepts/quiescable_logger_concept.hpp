// nexigen::logging — the quiescence contract for loggers (change
// keep-telemetry-handles-valid-until-infrastructure-destruction;
// spec logging/logger-quiescence).
//
// Quiescence is deliberately NOT part of `concepts::Logger`: the record surface
// is shared by every logger, while "stop the consumer for good" is a lifecycle
// concern the infrastructure needs only from the loggers it owns. Keeping it
// separate avoids forcing test doubles and non-app loggers to grow a stop
// operation they never use.
#pragma once

#include <concepts>

#include <nexigen/core/result.hpp>

namespace nexigen::logging::concepts {

/// @brief Structural contract for a logger that can be stopped for good.
///
/// A conforming logger offers `quiesce() noexcept` returning `core::Status`.
/// The call flushes accepted records, stops and joins the consumer, closes the
/// sink, and keeps the logger object alive; a repeated call is harmless.
///
/// @tparam T Candidate logger type.
/// @note Spec `logging/logger-quiescence`, requirement "Quiescable logger
///       contract".
template <class T>
concept QuiescableLogger = requires(T& logger) {
    { logger.quiesce() } noexcept -> std::same_as<core::Status>;
};

} // namespace nexigen::logging::concepts
