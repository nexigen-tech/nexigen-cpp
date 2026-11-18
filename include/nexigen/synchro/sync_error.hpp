// nexigen::synchro — structured failure value
// (change add-synchro-event-sync; spec synchro/protocol).
//
// One error type for every fallible synchronization entry point. The `kind`
// discriminates which cause field is meaningful, so callers can branch on the
// area without inspecting strings:
//
//   InvalidConfig -> core = InvalidArgument
//   Transport     -> transport = original com::Error
//   Metrics       -> core = original core::ErrorCode
//   Clock/EventFd/Thread -> core = SystemError, os_errno = errno
//   Lifecycle     -> core = Conflict/ResourceExhausted
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/com/error.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/synchro/sync_error_kind.hpp>

namespace nexigen::synchro {

/// @brief Structured description of a synchronization failure.
///
/// @note Spec `synchro/protocol`, requirement "Synchronization error model".
struct SyncError {
    SyncErrorKind kind{SyncErrorKind::Lifecycle}; ///< Failing area.
    com::Error transport{com::Error::Ok};         ///< Meaningful for `Transport`.
    core::ErrorCode core{core::ErrorCode::Ok};    ///< Meaningful for every other kind.
    int os_errno{0};                              ///< OS error number for clock/eventfd/thread.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable error summary.
    /// @note Spec `synchro/protocol`, requirement "Synchronization error model";
    ///       mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Serialize an error to diagnostic JSON.
/// @param j Destination JSON object.
/// @param error Error to serialize.
/// @note Mandate 10; cold path only.
void to_json(nlohmann::json& j, const SyncError& error);

/// @brief Parse an error from diagnostic JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param error Destination error, untouched on a non-object input.
/// @note Mandate 10; cold path only.
void from_json(const nlohmann::json& j, SyncError& error) noexcept;

/// @brief Value-or-`SyncError` result used by every fallible entry point.
/// @tparam T Success value type.
template <class T> using SyncResult = core::Result<T, SyncError>;

/// @brief Status-or-`SyncError` result.
using SyncStatus = core::Result<void, SyncError>;

/// @brief Build a configuration error (`core = InvalidArgument`).
/// @return Error with kind `InvalidConfig`.
[[nodiscard]] inline SyncError make_config_error() noexcept {
    return SyncError{SyncErrorKind::InvalidConfig, com::Error::Ok, core::ErrorCode::InvalidArgument,
                     0};
}

/// @brief Build a transport error around a `com` failure.
/// @param transport Original transport error.
/// @return Error with kind `Transport` and the original cause preserved.
[[nodiscard]] inline SyncError make_transport_error(com::Error transport) noexcept {
    return SyncError{SyncErrorKind::Transport, transport, core::ErrorCode::Ok, 0};
}

/// @brief Build a metrics error around a `core` failure.
/// @param cause Original registry error code.
/// @return Error with kind `Metrics` and the original cause preserved.
[[nodiscard]] inline SyncError make_metrics_error(core::ErrorCode cause) noexcept {
    return SyncError{SyncErrorKind::Metrics, com::Error::Ok, cause, 0};
}

/// @brief Build an operating-system error.
/// @param kind Failing area (`Clock`, `EventFd`, or `Thread`).
/// @param os_error Captured `errno` value.
/// @return Error with `core = SystemError` and the OS cause.
[[nodiscard]] inline SyncError make_os_error(SyncErrorKind kind, int os_error) noexcept {
    return SyncError{kind, com::Error::Ok, core::ErrorCode::SystemError, os_error};
}

/// @brief Build a lifecycle error.
/// @param cause Core classification (`Conflict` or `ResourceExhausted`).
/// @return Error with kind `Lifecycle`.
[[nodiscard]] inline SyncError make_lifecycle_error(core::ErrorCode cause) noexcept {
    return SyncError{SyncErrorKind::Lifecycle, com::Error::Ok, cause, 0};
}

} // namespace nexigen::synchro
