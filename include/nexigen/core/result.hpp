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

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

// The serialization mandate machinery travels WITH the vocabulary: every
// module pulls result.hpp, so the generic enum/struct formatters and the
// generic nexigen::to_json/from_json pair are always in scope where per-type
// declarations used to be written out (refactor-serialization-mandate).
// The enum vocabulary engine rides along for the same reason
// (refactor-enum-traits-helper): modules get EnumTraits + the json glue in
// scope from the header they already include.
#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>
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
    Ok,                ///< Success enumerator; not expected inside a Result's error arm.
    BufferFull,        ///< Bounded queue rejected a push: all capacity is in use.
    BufferEmpty,       ///< Bounded queue had nothing to pop.
    InvalidArgument,   ///< Caller-supplied value violated a precondition.
    SystemError,       ///< An OS call failed (errno-era failure, e.g. clock_gettime).
    NotInitialized,    ///< Operation attempted before required setup completed.
    RateLimited,       ///< Rate policy rejected the request before any queue interaction.
    Conflict,          ///< Another owner holds a process-global resource/state.
    ResourceExhausted, ///< Fixed-capacity pool of a preallocated resource is fully issued.
};

/// ErrorCode vocabulary table (change refactor-enum-traits-helper): the single
/// source of truth for the wire names. Append-only contract holds: existing
/// rows keep their numeric values and spellings; the test TU pins every name
/// with static_assert, compensating for the lost -Wswitch coverage.
template <> struct EnumTraits<ErrorCode> {
    struct Entry {
        ErrorCode value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 9> entries{{
        {ErrorCode::Ok, "Ok"},
        {ErrorCode::BufferFull, "BufferFull"},
        {ErrorCode::BufferEmpty, "BufferEmpty"},
        {ErrorCode::InvalidArgument, "InvalidArgument"},
        {ErrorCode::SystemError, "SystemError"},
        {ErrorCode::NotInitialized, "NotInitialized"},
        {ErrorCode::RateLimited, "RateLimited"},
        {ErrorCode::Conflict, "Conflict"},
        {ErrorCode::ResourceExhausted, "ResourceExhausted"},
    }};
};

/// Compile-time name of an error code; static-storage view, no allocation, no
/// exception path. Values outside the table render "Unknown" (engine
/// fallback — well-defined for a garbage-cast enum).
[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
    return enum_name(code);
}

/// Parse a canonical enumerator name (the exact to_string spelling). Returns
/// false leaving the out-parameter UNCHANGED on any other spelling — including
/// "Unknown", which is a rendering, not an enumerator. constexpr in C++23.
[[nodiscard]] constexpr bool from_string(std::string_view name, ErrorCode& out) noexcept {
    return enum_value(name, out);
}

/// ADL JSON writer (cold path): the value is exactly to_string(code).
inline void to_json(nlohmann::json& j, const ErrorCode code) {
    mandate::enum_to_json(j, code);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1: get_ptr
/// yields nullptr for any non-string value (numbers/objects/arrays/null), an
/// unknown name leaves `out` unchanged (caller pre-sets the fallback). Never
/// aborts regardless of input.
inline void from_json(const nlohmann::json& j, ErrorCode& out) noexcept {
    mandate::enum_from_json(j, out);
}

/// Value-or-error return type: holds a T or an ErrorCode (custom E allowed).
/// Never call value(); use has_value(), *res, res->, res.error().
template <class T, class E = ErrorCode> using Result = std::expected<T, E>;

/// Outcome of an operation that carries no payload. Status{} means success.
/// Never call value(); use has_value() and .error().
using Status = std::expected<void, ErrorCode>;

} // namespace nexigen::core
