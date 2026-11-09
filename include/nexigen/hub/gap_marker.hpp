// nexigen::hub — GapMarker DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::hub {

/// A missing sequence range in a stream ring.
struct GapMarker {
    std::string stream_id{};
    std::uint64_t dropped{0};
    std::uint64_t first_missing_sequence{0};
    std::uint64_t last_missing_sequence{0};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helper
[[nodiscard]] inline std::string to_string(const GapMarker& value) {
    return value.to_string();
}

// --- cold-path ADL serialization (bodies in src/hub/hub_format.cpp)
void to_json(nlohmann::json& j, const GapMarker& value);
void from_json(const nlohmann::json& j, GapMarker& value) noexcept;

} // namespace nexigen::hub
