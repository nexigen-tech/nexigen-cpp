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

/// Transport failures of the communication layer.
enum class Error : std::uint8_t {
    Ok,                      ///< Success.
    ServiceAlreadyExists,    ///< create() collided with an existing service name.
    ServiceNotFound,         ///< open() found no such service.
    CreationFailed,          ///< Port/service creation failed for any other reason.
    ConnectionFailure,       ///< A connected peer disappeared or could not be reached.
    IncompatibleTypeLayout,  ///< Payload type name/size/alignment mismatch on connect.
    OutOfSamples,            ///< The shared-memory loan pool is exhausted.
    UnableToDeliver,         ///< send() could not deliver (connection/delivery policy).
    Timeout,                 ///< A synchronous operation exceeded its deadline.
    InvalidArgument,         ///< Caller-supplied value violated a precondition.
    Unknown,                 ///< Unmapped underlying transport error.
};

/// The never-.value() rule of core::Result applies here verbatim.
template <class T, class E = Error> using Result = std::expected<T, E>;
using Status = std::expected<void, Error>;

} // namespace nexigen::com

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::com::Error> {
    struct Entry {
        nexigen::com::Error value;
        std::string_view name;
    };
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

/// Canonical name of an error; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(Error error) noexcept {
    return core::enum_name(error);
}

/// Parse a canonical Error name; false leaves the out-parameter UNCHANGED.
[[nodiscard]] constexpr bool from_string(std::string_view name, Error& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the value is exactly to_string(error).
inline void to_json(nlohmann::json& j, const Error error) {
    core::mandate::enum_to_json(j, error);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, Error& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::com
