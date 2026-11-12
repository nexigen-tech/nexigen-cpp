// nexigen::com — the service manifest (change add-com-module;
// spec com/service-management "Minimal service manifest").
//
// Value type listing the services an application provides and requires; the
// byte-level sizing is derived from the payload types at compile time, so the
// manifest carries only architectural parameters (depth, subscribers) and QoS
// flags.
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexigen::com {

/// @brief One provided (published) service in a `ServiceManifest`.
///
/// Lists the architectural parameters of a service this application publishes;
/// byte-level sizing is derived from the payload type at compile time, so the
/// manifest never carries raw byte counts.
///
/// @note Spec `com/service-management`, requirement "Minimal service manifest".
struct ProvidedService {
    std::string service_name{};       ///< Service identity.
    std::uint64_t max_subscribers{1}; ///< Upper bound on connected subscribers.
    std::uint64_t history_size{0}; ///< Samples kept for late subscribers; `0` = transport default.

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return A human-readable rendering of this entry's name and limits.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `com/service-management`, requirement "Minimal service
    ///       manifest".
    [[nodiscard]] std::string to_string() const;
};

/// @brief One required (consumed) service in a `ServiceManifest`.
///
/// Lists the architectural parameters of a service this application consumes;
/// byte-level sizing is derived from the payload type at compile time.
///
/// @note Spec `com/service-management`, requirement "Minimal service manifest".
struct RequiredService {
    std::string service_name{};         ///< Service identity.
    std::uint64_t buffer_capacity{2};   ///< Samples the subscriber may hold.
    bool report_overflow_to_phm{false}; ///< Report an overflow to the PHM.

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return A human-readable rendering of this entry's name and limits.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `com/service-management`, requirement "Minimal service
    ///       manifest".
    [[nodiscard]] std::string to_string() const;
};

/// @brief The communication section of an application manifest.
///
/// A value type listing the services the application provides and requires.
/// JSON round-trips without throwing under `JSON_NOEXCEPTION=1`, and an
/// invalid or hostile JSON input leaves the target manifest unchanged.
///
/// @note Spec `com/service-management`, requirement "Minimal service manifest".
struct ServiceManifest {
    std::vector<ProvidedService> provided_services{}; ///< Services this application publishes.
    std::vector<RequiredService> required_services{}; ///< Services this application consumes.

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return A human-readable rendering of both service lists.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `com/service-management`, requirement "Minimal service
    ///       manifest".
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `ServiceManifest::to_string()`.
///
/// @param manifest Manifest to render.
/// @return The manifest's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const ServiceManifest& manifest) {
    return manifest.to_string();
}

/// @brief ADL JSON writer for a manifest.
///
/// @param j Output JSON value, replaced by the manifest object.
/// @param manifest Manifest to serialize.
/// @note Mandate 10 (universal JSON); allocates, so cold path only. Spec
///       `com/service-management`, requirement "Minimal service manifest".
void to_json(nlohmann::json& j, const ServiceManifest& manifest);

/// @brief ADL JSON reader, strictly non-throwing under `JSON_NOEXCEPTION=1`.
///
/// @param j Input JSON value; a missing or malformed document leaves
///        `manifest` unchanged.
/// @param manifest Receives the parsed manifest on success.
/// @note Mandate 10 (universal JSON); spec `com/service-management`,
///       requirement "Minimal service manifest": a hostile input must not
///       throw and must leave the target untouched.
void from_json(const nlohmann::json& j, ServiceManifest& manifest) noexcept;

} // namespace nexigen::com

/// @brief Formats a `ServiceManifest` as its diagnostic string.
///
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `ServiceManifest::to_string()`, which allocates, so cold path only.
template <>
struct std::formatter<nexigen::com::ServiceManifest> : std::formatter<std::string_view> {
    /// @brief Write the manifest's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param manifest Manifest to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::ServiceManifest& manifest, Ctx& ctx) const {
        const std::string text = manifest.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
