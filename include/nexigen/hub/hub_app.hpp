// nexigen::hub — HubApp DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_instance.hpp>
#include <nexigen/runner/app_state.hpp>

namespace nexigen::hub {

/// One application and all of its instances.
struct HubApp {
    std::string app{};
    runner::AppState state{runner::AppState::Unknown};
    std::vector<HubInstance> instances{};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helper
[[nodiscard]] inline std::string to_string(const HubApp& value) {
    return value.to_string();
}

// --- cold-path ADL serialization (bodies in src/hub/hub_format.cpp)
void to_json(nlohmann::json& j, const HubApp& value);
void from_json(const nlohmann::json& j, HubApp& value) noexcept;

} // namespace nexigen::hub
