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
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

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

/// Parse a canonical enumerator name (the exact to_string spelling). Returns
/// false leaving the out-parameter UNCHANGED on any other spelling — including
/// "Unknown", which is a rendering, not an enumerator. constexpr in C++23.
[[nodiscard]] constexpr bool from_string(std::string_view name, ErrorCode& out) noexcept {
    if (name == "Ok") { out = ErrorCode::Ok; return true; }
    if (name == "BufferFull") { out = ErrorCode::BufferFull; return true; }
    if (name == "BufferEmpty") { out = ErrorCode::BufferEmpty; return true; }
    if (name == "InvalidArgument") { out = ErrorCode::InvalidArgument; return true; }
    if (name == "SystemError") { out = ErrorCode::SystemError; return true; }
    if (name == "NotInitialized") { out = ErrorCode::NotInitialized; return true; }
    if (name == "RateLimited") { out = ErrorCode::RateLimited; return true; }
    return false;
}

/// ADL JSON writer (cold path only — nlohmann::json is banned on hot paths):
/// the JSON value is exactly to_string(code). Allocation happens here; never
/// call from a producer/worker loop.
inline void to_json(nlohmann::json& j, const ErrorCode code) {
    j = std::string(to_string(code));
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1: get_ptr
/// yields nullptr for any non-string value (numbers/objects/arrays/null), an
/// unknown name leaves `out` unchanged (caller pre-sets the fallback). Never
/// aborts regardless of input.
inline void from_json(const nlohmann::json& j, ErrorCode& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
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

/// ErrorCode formats as its canonical name (mandate 11): delegates parse and
/// fill/align handling to the string_view formatter, formats over to_string.
/// The context is templated: libstdc++ 15 evaluates formattability against a
/// substituted context, and a hard-coded std::format_context& would not bind.
template <>
struct std::formatter<nexigen::core::ErrorCode> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::core::ErrorCode code, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::core::to_string(code), ctx);
    }
};
