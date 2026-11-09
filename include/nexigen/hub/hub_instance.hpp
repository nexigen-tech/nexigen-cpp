// nexigen::hub — HubInstance DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_attempt.hpp>
#include <nexigen/runner/app_state.hpp>

namespace nexigen::hub {

/// One application instance (control directory + attempts).
struct HubInstance {
    std::string instance{};
    std::string instance_dir{};
    runner::AppState state{runner::AppState::Unknown};
    std::vector<HubAttempt> attempts{};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helper
[[nodiscard]] inline std::string to_string(const HubInstance& value) {
    return value.to_string();
}

// --- cold-path ADL serialization (bodies in src/hub/hub_format.cpp)
void to_json(nlohmann::json& j, const HubInstance& value);
void from_json(const nlohmann::json& j, HubInstance& value) noexcept;

} // namespace nexigen::hub
