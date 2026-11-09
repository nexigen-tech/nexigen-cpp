// nexigen::hub — HubRun DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_instance.hpp>
#include <nexigen/hub/hub_suite.hpp>

namespace nexigen::hub {

/// One run root.
struct HubRun {
    std::string run_key{};
    std::string run_dir{};
    std::vector<HubSuite> suites{};
    std::vector<HubInstance> standalone_instances{};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helper
[[nodiscard]] inline std::string to_string(const HubRun& value) {
    return value.to_string();
}

// --- cold-path ADL serialization (bodies in src/hub/hub_format.cpp)
void to_json(nlohmann::json& j, const HubRun& value);
void from_json(const nlohmann::json& j, HubRun& value) noexcept;

} // namespace nexigen::hub
