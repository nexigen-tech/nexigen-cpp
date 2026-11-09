// nexigen::hub — HubSuite DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_app.hpp>
#include <nexigen/hub/hub_stream.hpp>
#include <nexigen/exec/suite_state.hpp>

namespace nexigen::hub {

/// One suite and its applications.
struct HubSuite {
    std::string suite_id{};
    std::string suite_dir{};
    std::string artifacts_path{};
    exec::SuiteState state{exec::SuiteState::Unknown};
    std::uint32_t suite_restarts_used{0};
    std::string reason{};
    HubStream execution_manager_log{};
    std::vector<HubApp> apps{};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helper
[[nodiscard]] inline std::string to_string(const HubSuite& value) {
    return value.to_string();
}

// --- cold-path ADL serialization (bodies in src/hub/hub_format.cpp)
void to_json(nlohmann::json& j, const HubSuite& value);
void from_json(const nlohmann::json& j, HubSuite& value) noexcept;

} // namespace nexigen::hub
