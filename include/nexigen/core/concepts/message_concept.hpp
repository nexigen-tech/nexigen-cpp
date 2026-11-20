// nexigen::core — the payload contract shared by modules that exchange plain
// bytes (change unify-core-concepts; spec core/concepts "Message concept").
//
// Satisfied exactly when std::is_trivially_copyable_v<T> holds. Pointers,
// virtual tables, and owning members are rejected at compile time; a payload
// must survive a zero-copy shared-memory exchange unchanged.
#pragma once

#include <concepts>
#include <type_traits>

namespace nexigen::core::concepts {

/// @brief Payload contract for shared-memory value types.
///
/// Satisfied exactly when `std::is_trivially_copyable_v<T>` holds. Naming the
/// concept in `core` lets consumers in different modules state the same
/// constraint without depending on the module that transports the bytes.
///
/// @tparam T Candidate payload type.
/// @note Spec `core/concepts`, requirement "Message concept".
template <class T>
concept Message = std::is_trivially_copyable_v<T>;

} // namespace nexigen::core::concepts
