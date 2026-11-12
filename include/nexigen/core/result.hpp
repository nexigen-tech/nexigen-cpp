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

/// @brief Failure classifications reported by the core layer.
///
/// Fixed `std::uint8_t` underlying type keeps `Result<T>` small and
/// array-friendly. Append-only contract: existing enumerators keep their
/// numeric values, so recompiled and non-recompiled translation units stay
/// ABI-compatible.
///
/// @note Spec `core/result`, requirement "Error code catalogue".
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

/// @brief Vocabulary table for `ErrorCode` (`core::enum_name`/`enum_value`).
///
/// The single source of truth for the wire names. The append-only contract
/// holds: existing rows keep their numeric values and spellings, and the test
/// TU pins every name with `static_assert`, compensating for the lost
/// `-Wswitch` coverage.
template <> struct EnumTraits<ErrorCode> {
    /// @brief One table row: an error code and its canonical spelling.
    struct Entry {
        ErrorCode value;       ///< Error code described by the row.
        std::string_view name; ///< Canonical PascalCase wire name.
    };
    /// @brief Name table covering every enumerator in declaration order.
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

/// @brief Compile-time name of an error code.
///
/// Static-storage view, no allocation, no exception path.
///
/// @param code Error code to render.
/// @return Canonical name; values outside the table render `"Unknown"` (the
///         engine fallback, well-defined for a garbage-cast enum).
/// @note Spec `core/result`, requirement "Compile-time error code naming";
///       mandate 9.
[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
    return enum_name(code);
}

/// @brief Parse a canonical enumerator name.
///
/// @param name Candidate spelling (the exact `to_string` output).
/// @param out Destination, untouched on any other spelling.
/// @return `true` with the enumerator on an exact match; `false` otherwise,
///         including `"Unknown"` (a rendering, not an enumerator).
/// @note Spec `core/result`, requirement "ErrorCode name parsing".
[[nodiscard]] constexpr bool from_string(std::string_view name, ErrorCode& out) noexcept {
    return enum_value(name, out);
}

/// @brief ADL JSON writer: the value is exactly `to_string(code)`.
///
/// @param j JSON value overwritten with the name string.
/// @param code Error code to serialize.
/// @note Spec `core/result`, requirement "ErrorCode JSON serialization";
///       mandate 10. Cold path only.
inline void to_json(nlohmann::json& j, const ErrorCode code) {
    mandate::enum_to_json(j, code);
}

/// @brief ADL JSON reader, strictly non-throwing under `JSON_NOEXCEPTION=1`.
///
/// A non-string value (number, object, array, null) yields no pointer, and an
/// unknown name is rejected; in both cases `out` is unchanged (the caller
/// pre-sets its fallback).
///
/// @param j JSON value to read.
/// @param out Destination, untouched on a non-string value or unknown name.
/// @note Spec `core/result`, requirement "ErrorCode JSON serialization";
///       mandate 10. Never aborts regardless of input.
inline void from_json(const nlohmann::json& j, ErrorCode& out) noexcept {
    mandate::enum_from_json(j, out);
}

/// @brief Value-or-error return type: holds a `T` or an `E` (default
///        `ErrorCode`).
///
/// Alias for `std::expected`: no heap allocation, no locks, no exception path,
/// so it is safe to return by value on real-time hot paths.
///
/// @tparam T Success payload type.
/// @tparam E Failure type; `ErrorCode` unless the caller names another.
/// @note Spec `core/result`, requirement "Result value-or-error type".
/// @warning Never call `.value()` or an unchecked `.error()`: under
///          `-fno-exceptions` it degenerates to `std::abort()`. Branch on
///          `has_value()` and read `*res` / `res->`, or `res.error()` only
///          after the branch.
template <class T, class E = ErrorCode> using Result = std::expected<T, E>;

/// @brief Outcome of an operation that carries no payload; `Status{}` means
///        success.
///
/// Alias for `std::expected<void, ErrorCode>`.
///
/// @note Spec `core/result`, requirement "Status type for operations with no
///       value".
/// @warning Never call `.value()`; branch on `has_value()` and read `.error()`.
using Status = std::expected<void, ErrorCode>;

} // namespace nexigen::core
