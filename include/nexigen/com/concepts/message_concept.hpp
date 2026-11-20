// nexigen::com — the payload contract (change add-com-module;
// spec com/publish-subscribe "Trivially-copyable message contract").
//
// Unified onto core::concepts::Message by change unify-core-concepts: the
// constraint stays visible under the com name, but its definition no longer
// lives in this module.
#pragma once

#include <nexigen/core/concepts/message_concept.hpp>

namespace nexigen::com::concepts {

/// @brief Payload contract for every `com` port template.
///
/// An alias of `core::concepts::Message<T>`, satisfied exactly when
/// `std::is_trivially_copyable_v<T>` holds.
///
/// @tparam T Candidate payload type.
/// @note Spec `com/publish-subscribe`, requirement "Trivially-copyable message
///       contract".
template <class T>
concept Message = core::concepts::Message<T>;

} // namespace nexigen::com::concepts
