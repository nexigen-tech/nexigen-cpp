// nexigen::hub — StreamRecord DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/hub/stream_kind.hpp>

namespace nexigen::hub {

/// One complete line from a stream.
struct StreamRecord {
    std::string stream_id{};
    StreamKind kind{StreamKind::App};
    std::uint64_t sequence{0};
    std::string received_at_rfc3339{};
    std::string line{};
    bool truncated{false};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helper
[[nodiscard]] inline std::string to_string(const StreamRecord& value) {
    return value.to_string();
}

// --- cold-path ADL serialization (bodies in src/hub/hub_format.cpp)
void to_json(nlohmann::json& j, const StreamRecord& value);
void from_json(const nlohmann::json& j, StreamRecord& value) noexcept;

} // namespace nexigen::hub
