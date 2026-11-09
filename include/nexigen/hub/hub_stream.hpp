// nexigen::hub — HubStream DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/hub/stream_kind.hpp>

namespace nexigen::hub {

/// One tailed file.
struct HubStream {
    std::string id{};
    StreamKind kind{StreamKind::App};
    std::string path{};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helper
[[nodiscard]] inline std::string to_string(const HubStream& value) {
    return value.to_string();
}

// --- cold-path ADL serialization (bodies in src/hub/hub_format.cpp)
void to_json(nlohmann::json& j, const HubStream& value);
void from_json(const nlohmann::json& j, HubStream& value) noexcept;

} // namespace nexigen::hub
