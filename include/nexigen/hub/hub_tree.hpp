// nexigen::hub — HubTree DTO + tree queries (change
// refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_run.hpp>
#include <nexigen/hub/hub_stream.hpp>

namespace nexigen::hub {

/// Complete discovered tree.
struct HubTree {
    std::vector<HubRun> runs{};

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] bool empty() const noexcept { return runs.empty(); }
};

// --- free to_string helper
[[nodiscard]] inline std::string to_string(const HubTree& value) {
    return value.to_string();
}

// --- cold-path ADL serialization (bodies in src/hub/hub_format.cpp)
void to_json(nlohmann::json& j, const HubTree& value);
void from_json(const nlohmann::json& j, HubTree& value) noexcept;

/// Flatten all streams in a tree (including suite runner logs).
[[nodiscard]] std::vector<HubStream> all_streams(const HubTree& tree);
/// Find one stream by id, or nullptr.
[[nodiscard]] const HubStream* find_stream(const HubTree& tree, std::string_view id) noexcept;

} // namespace nexigen::hub
