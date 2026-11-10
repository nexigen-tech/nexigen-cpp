// nexigen::com — the payload contract (change add-com-module;
// spec com/publish-subscribe "Trivially-copyable message contract").
//
// Every com port template constrains its payload on Message<T>: satisfied
// exactly when std::is_trivially_copyable_v<T> holds. Pointers, virtual
// tables, and owning members are rejected at compile time — the payload must
// be plain bytes that survive a zero-copy shared-memory exchange unchanged.
#pragma once

#include <concepts>
#include <type_traits>

namespace nexigen::com::concepts {

/// A payload type safe for zero-copy transfer: trivially copyable, no
/// pointers, no virtual functions, no owning members.
template <class T>
concept Message = std::is_trivially_copyable_v<T>;

} // namespace nexigen::com::concepts
