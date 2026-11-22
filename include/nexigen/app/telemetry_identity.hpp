// nexigen::app — identity handed to an infrastructure policy (change
// widen-app-id-to-16-bytes; spec app/policies).
#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/app_id.hpp>

namespace nexigen::app {

/// @brief Application identity passed to the policy hooks.
///
/// `app_name` is the long, human-readable name used by file sinks;
/// `app_id` is the short canonical code carried by the transport frames. The
/// two travel together so a policy never has to guess which one to use. A
/// default-constructed identity carries no name and an unset id, which lets
/// `Iceoryx2Policy` fall back to its compile-time tag.
///
/// The struct is a borrowed cold-path value: `app_name` points at the
/// `Settings` string, and `app_id` is an inline 16-byte value. It is consumed
/// once during `Infrastructure::initialize` and never stored.
///
/// @note Spec `app/policies`, requirement "Iceoryx2 policy publishes all
///       telemetry as fixed frames".
/// @note Cold path only: constructed once during initialization.
struct TelemetryIdentity {
    std::string_view app_name{}; ///< Long application name (empty = unset).
    core::AppId app_id{};        ///< Canonical 16-byte code (empty = fallback).

    /// @brief Render the identity as a cold-path diagnostic string.
    ///
    /// @return Text naming the application name and identifier.
    /// @note Project mandate 9; allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return "TelemetryIdentity(app_name=\"" + std::string(app_name) + "\", app_id=\"" +
               app_id.to_string() + "\")";
    }
};

/// @brief Free helper mirroring `TelemetryIdentity::to_string()`.
///
/// @param identity Identity to render.
/// @return The same text the member returns.
/// @note Project mandate 9.
[[nodiscard]] inline std::string to_string(const TelemetryIdentity& identity) {
    return identity.to_string();
}

/// @brief Serialize the identity for cold-path diagnostics.
///
/// @param j Destination value.
/// @param identity Identity to serialize.
/// @note Project mandate 10.
inline void to_json(nlohmann::json& j, const TelemetryIdentity& identity) {
    j = nlohmann::json{{"app_name", std::string(identity.app_name)}, {"app_id", identity.app_id}};
}

} // namespace nexigen::app
