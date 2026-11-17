// nexigen::com — event payload contract.
#pragma once

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::com::concepts {

/// @brief Payload contract for the event system.
///
/// This is the event-specific name for the same trivially-copyable shared-memory
/// contract as `Message<T>`. It is intentionally a named concept so the event
/// system can add constraints later without changing every signature.
///
/// @tparam T Candidate event payload type.
/// @note Spec `com/event-system`, requirement "Event payload contract";
///       references the `com/publish-subscribe` "Trivially-copyable message
///       contract".
template <class T>
concept EventPayload = Message<T>;

} // namespace nexigen::com::concepts
