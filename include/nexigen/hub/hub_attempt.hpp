// nexigen::hub — HubAttempt DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_stream.hpp>

namespace nexigen::hub {

/// One launch attempt and its streams.
struct HubAttempt {
    std::uint32_t attempt{0};
    std::string artifact_dir{};
    std::vector<HubStream> streams{};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helper
[[nodiscard]] inline std::string to_string(const HubAttempt& value) {
    return value.to_string();
}

// --- cold-path ADL serialization (bodies in src/hub/hub_format.cpp)
void to_json(nlohmann::json& j, const HubAttempt& value);
void from_json(const nlohmann::json& j, HubAttempt& value) noexcept;

} // namespace nexigen::hub
