// nexigen::com — error vocabulary (change add-com-module;
// spec com/service-management "Error vocabulary").
//
// Own error enum, deliberately separate from core::ErrorCode: the com module
// reports its transport failures in its own vocabulary so the IPC layer does
// not widen the core error surface. Full conversion set (mandates 9-11):
// constexpr to_string/from_string over core::EnumTraits, non-throwing ADL
// to_json/from_json under JSON_NOEXCEPTION=1, and the shared generic
// std::formatter.
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::com {

/// @brief Transport failures reported by the communication layer.
///
/// A vocabulary deliberately separate from `core::ErrorCode`: `com` reports
/// its IPC failures in its own enum so the transport layer does not widen the
/// core error surface. Every value round-trips through the project conversion
/// set (`to_string`/`from_string`, ADL `to_json`/`from_json`, and the shared
/// `std::formatter`).
///
/// @note Spec `com/service-management`, requirement "Error vocabulary".
///       Mandates 9/10/11 cover `to_string`, the ADL JSON pair, and the
///       generic `std::formatter`. Enumerator order is the canonical name
///       table order; an out-of-range cast renders `Unknown`.
enum class Error : std::uint8_t {
    Ok,                     ///< Success.
    ServiceAlreadyExists,   ///< create() collided with an existing service name.
    ServiceNotFound,        ///< open() found no such service.
    CreationFailed,         ///< Port/service creation failed for any other reason.
    ConnectionFailure,      ///< A connected peer disappeared or could not be reached.
    IncompatibleTypeLayout, ///< Payload type name/size/alignment mismatch on connect.
    OutOfSamples,           ///< The shared-memory loan pool is exhausted.
    UnableToDeliver,        ///< send() could not deliver (connection/delivery policy).
    Timeout,                ///< A synchronous operation exceeded its deadline.
    InvalidArgument,        ///< Caller-supplied value violated a precondition.
    Unknown,                ///< Unmapped underlying transport error.
};

/// @brief `std::expected` result over the `com` error vocabulary.
///
/// @tparam T Success value type.
/// @tparam E Error type; defaults to `com::Error`.
/// @note Spec `com/service-management`, requirement "Error vocabulary". The
///       never-`value()` rule of the project applies: callers test the result
///       and handle the error branch instead of extracting the value blindly.
template <class T, class E = Error> using Result = std::expected<T, E>;

/// @brief `std::expected` status with no success value.
///
/// Holds either an empty success or one `com::Error`.
///
/// @note Spec `com/service-management`, requirement "Error vocabulary".
using Status = std::expected<void, Error>;

} // namespace nexigen::com

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table binding `com::Error` to its canonical names.
///
/// Consumed by `core::enum_name` / `core::enum_value`; entry order is the
/// canonical round-trip order.
///
/// @note Spec `com/service-management`, requirement "Error vocabulary".
template <> struct EnumTraits<nexigen::com::Error> {
    /// @brief One value/name pair of the vocabulary table.
    struct Entry {
        nexigen::com::Error value; ///< Enumerator.
        std::string_view name;     ///< Canonical enumerator name.
    };
    /// @brief Canonical table; exactly one entry per enumerator.
    static constexpr std::array<Entry, 11> entries{{
        {nexigen::com::Error::Ok, "Ok"},
        {nexigen::com::Error::ServiceAlreadyExists, "ServiceAlreadyExists"},
        {nexigen::com::Error::ServiceNotFound, "ServiceNotFound"},
        {nexigen::com::Error::CreationFailed, "CreationFailed"},
        {nexigen::com::Error::ConnectionFailure, "ConnectionFailure"},
        {nexigen::com::Error::IncompatibleTypeLayout, "IncompatibleTypeLayout"},
        {nexigen::com::Error::OutOfSamples, "OutOfSamples"},
        {nexigen::com::Error::UnableToDeliver, "UnableToDeliver"},
        {nexigen::com::Error::Timeout, "Timeout"},
        {nexigen::com::Error::InvalidArgument, "InvalidArgument"},
        {nexigen::com::Error::Unknown, "Unknown"},
    }};
};

} // namespace nexigen::core

namespace nexigen::com {

/// @brief Canonical name of an error.
///
/// @param error Error value to render; an out-of-range cast renders
///        `"Unknown"`.
/// @return The canonical enumerator name.
/// @note Mandate 9 (universal `to_string`); allocation-free and `constexpr`,
///       so it may be called on a hot path. Spec `com/service-management`,
///       requirement "Error vocabulary".
[[nodiscard]] constexpr std::string_view to_string(Error error) noexcept {
    return core::enum_name(error);
}

/// @brief Parse a canonical error name.
///
/// @param name Candidate canonical name, as produced by `to_string`.
/// @param out Receives the parsed value; left UNCHANGED when parsing fails.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Mandate 9; allocation-free and `constexpr`. Spec
///       `com/service-management`, requirement "Error vocabulary".
[[nodiscard]] constexpr bool from_string(std::string_view name, Error& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer: the value is exactly `to_string(error)`.
///
/// @param j Output JSON value, replaced by the canonical name `string`.
/// @param error Error to serialize.
/// @note Mandate 10 (universal JSON); allocates, so cold path only. Spec
///       `com/service-management`, requirement "Error vocabulary".
inline void to_json(nlohmann::json& j, const Error error) {
    core::mandate::enum_to_json(j, error);
}

/// @brief ADL JSON reader, strictly non-throwing under `JSON_NOEXCEPTION=1`.
///
/// @param j Input JSON value; a non-string or unknown name leaves `out`
///        unchanged.
/// @param out Receives the parsed error on success.
/// @note Mandate 10 (universal JSON) and spec `com/service-management`,
///       requirement "Error vocabulary": a hostile input must not throw and
///       must leave the target untouched.
inline void from_json(const nlohmann::json& j, Error& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::com
