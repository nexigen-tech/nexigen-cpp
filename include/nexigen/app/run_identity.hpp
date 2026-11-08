// nexigen::app — identity of one application instance inside one run (spec app/telemetry-layout).
#pragma once
#include <string>
#include <nlohmann/json.hpp>
namespace nexigen::app {
struct RunIdentity {
    std::string run_id{};
    std::string run_dir{};
    std::string instance_id{};
    std::string instance_dir{};
    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const RunIdentity& identity) {
    return identity.to_string();
}
void to_json(nlohmann::json& j, const RunIdentity& identity);
void from_json(const nlohmann::json& j, RunIdentity& identity) noexcept;
} // namespace nexigen::app
