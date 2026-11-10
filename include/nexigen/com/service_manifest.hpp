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

/// One provided (published) service.
struct ProvidedService {
    std::string service_name{};
    std::uint64_t max_subscribers{1};
    std::uint64_t history_size{0};

    [[nodiscard]] std::string to_string() const;
};

/// One required (consumed) service.
struct RequiredService {
    std::string service_name{};
    std::uint64_t buffer_capacity{2};
    bool report_overflow_to_phm{false};

    [[nodiscard]] std::string to_string() const;
};

/// The communication section of an application manifest.
struct ServiceManifest {
    std::vector<ProvidedService> provided_services{};
    std::vector<RequiredService> required_services{};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const ServiceManifest& manifest) {
    return manifest.to_string();
}

/// Cold-path ADL serialization (mandate 10), non-throwing readers under
/// JSON_NOEXCEPTION=1.
void to_json(nlohmann::json& j, const ServiceManifest& manifest);
void from_json(const nlohmann::json& j, ServiceManifest& manifest) noexcept;

} // namespace nexigen::com

/// The manifest formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::com::ServiceManifest> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::ServiceManifest& manifest, Ctx& ctx) const {
        const std::string text = manifest.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
