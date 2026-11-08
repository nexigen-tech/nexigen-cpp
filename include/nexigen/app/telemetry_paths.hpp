// nexigen::app — resolved output locations (spec app/telemetry-layout).
#pragma once
#include <string>
#include <nlohmann/json.hpp>
namespace nexigen::app {
struct TelemetryPaths {
    std::string metrics{};
    std::string traces{};
    std::string chrono{};
    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const TelemetryPaths& paths) {
    return paths.to_string();
}
void to_json(nlohmann::json& j, const TelemetryPaths& paths);
void from_json(const nlohmann::json& j, TelemetryPaths& paths) noexcept;
} // namespace nexigen::app
