// nexigen::core — exception-free error model (design.md D1).
//
// Result<T, E = ErrorCode> is std::expected<T, E>; Status is
// std::expected<void, ErrorCode>. Every fallible nexigen API returns one of
// these by value: no heap allocation, no locks, no exception paths.
//
// HARD USAGE RULE — never call value() (or unchecked error()) on a Result:
// under -fno-exceptions it degenerates to std::abort(), which looks like a
// crash in production. Branch on has_value() and read the payload via *res /
// res-> and the failure via res.error() only after the branch.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

namespace nexigen::core {

/// Failure classifications reported by the core layer.
/// Fixed uint8_t underlying type keeps Result<T> small and array-friendly.
/// Append-only contract: existing enumerators keep their numeric values, so
/// recompiled and non-recompiled translation units stay ABI-compatible.
enum class ErrorCode : std::uint8_t {
    Ok,             ///< Success enumerator; not expected inside a Result's error arm.
    BufferFull,     ///< Bounded queue rejected a push: all capacity is in use.
    BufferEmpty,    ///< Bounded queue had nothing to pop.
    InvalidArgument,///< Caller-supplied value violated a precondition.
    SystemError,    ///< An OS call failed (errno-era failure, e.g. clock_gettime).
    NotInitialized, ///< Operation attempted before required setup completed.
    RateLimited,    ///< Rate policy rejected the request before any queue interaction.
};

/// Compile-time name of an error code; returns a static-storage view, no
/// allocation, no exception path. The `default` arm makes a garbage-cast enum
/// value well-defined ("Unknown"); every enumerator is pinned by static_assert
/// in the test TU, so the loss of -Wswitch coverage is compensated.
[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Ok:             return "Ok";
        case ErrorCode::BufferFull:     return "BufferFull";
        case ErrorCode::BufferEmpty:    return "BufferEmpty";
        case ErrorCode::InvalidArgument:return "InvalidArgument";
        case ErrorCode::SystemError:    return "SystemError";
        case ErrorCode::NotInitialized: return "NotInitialized";
        case ErrorCode::RateLimited:    return "RateLimited";
        default:                        return "Unknown";
    }
}

/// Value-or-error return type: holds a T or an ErrorCode (custom E allowed).
/// Never call value(); use has_value(), *res, res->, res.error().
template <class T, class E = ErrorCode>
using Result = std::expected<T, E>;

/// Outcome of an operation that carries no payload. Status{} means success.
/// Never call value(); use has_value() and .error().
using Status = std::expected<void, ErrorCode>;

} // namespace nexigen::core
