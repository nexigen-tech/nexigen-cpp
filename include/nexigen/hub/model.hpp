// nexigen::hub — read-only observatory model
// (change nexigen-hub; specs hub/discovery, hub/streaming).
//
// COLD PATH ONLY. These types are used by the hub control loop, HTTP
// snapshots, and stream replay; they never run in an application hot path.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/runner/app_suite.hpp>

namespace nexigen::hub {

/// Runtime stream family.
enum class StreamKind : std::uint8_t {
    App,
    Metrics,
    Traces,
    Chrono,
    Stdout,
    Stderr,
    Runner,
};

[[nodiscard]] constexpr std::string_view to_string(StreamKind kind) noexcept {
    switch (kind) {
        case StreamKind::App:     return "app";
        case StreamKind::Metrics: return "metrics";
        case StreamKind::Traces:  return "traces";
        case StreamKind::Chrono:  return "chrono";
        case StreamKind::Stdout:  return "stdout";
        case StreamKind::Stderr:  return "stderr";
        case StreamKind::Runner:  return "runner";
        default:                   return "unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         StreamKind& out) noexcept {
    if (name == "app")     { out = StreamKind::App;     return true; }
    if (name == "metrics") { out = StreamKind::Metrics; return true; }
    if (name == "traces")  { out = StreamKind::Traces;  return true; }
    if (name == "chrono")  { out = StreamKind::Chrono;  return true; }
    if (name == "stdout")  { out = StreamKind::Stdout;  return true; }
    if (name == "stderr")  { out = StreamKind::Stderr;  return true; }
    if (name == "runner")  { out = StreamKind::Runner;  return true; }
    return false;
}

/// One tailed file.
struct HubStream {
    std::string id{};
    StreamKind kind{StreamKind::App};
    std::string path{};

    [[nodiscard]] std::string to_string() const;
};

/// One launch attempt and its streams.
struct HubAttempt {
    std::uint32_t attempt{0};
    std::string artifact_dir{};
    std::vector<HubStream> streams{};

    [[nodiscard]] std::string to_string() const;
};

/// One application instance (control directory + attempts).
struct HubInstance {
    std::string instance{};
    std::string instance_dir{};
    runner::AppState state{runner::AppState::Unknown};
    std::vector<HubAttempt> attempts{};

    [[nodiscard]] std::string to_string() const;
};

/// One application and all of its instances.
struct HubApp {
    std::string app{};
    runner::AppState state{runner::AppState::Unknown};
    std::vector<HubInstance> instances{};

    [[nodiscard]] std::string to_string() const;
};

/// One suite and its applications.
struct HubSuite {
    std::string suite_id{};
    std::string suite_dir{};
    std::string artifacts_path{};
    runner::SuiteState state{runner::SuiteState::Unknown};
    std::uint32_t suite_restarts_used{0};
    std::string reason{};
    HubStream runner_log{};
    std::vector<HubApp> apps{};

    [[nodiscard]] std::string to_string() const;
};

/// One run root.
struct HubRun {
    std::string run_key{};
    std::string run_dir{};
    std::vector<HubSuite> suites{};
    std::vector<HubInstance> standalone_instances{};

    [[nodiscard]] std::string to_string() const;
};

/// Complete discovered tree.
struct HubTree {
    std::vector<HubRun> runs{};

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] bool empty() const noexcept { return runs.empty(); }
};

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

/// A missing sequence range in a stream ring.
struct GapMarker {
    std::string stream_id{};
    std::uint64_t dropped{0};
    std::uint64_t first_missing_sequence{0};
    std::uint64_t last_missing_sequence{0};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helpers
[[nodiscard]] inline std::string to_string(const HubStream& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const HubAttempt& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const HubInstance& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const HubApp& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const HubSuite& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const HubRun& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const HubTree& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const StreamRecord& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const GapMarker& value) { return value.to_string(); }

// --- cold-path ADL serialization (bodies in src/hub/model.cpp)
void to_json(nlohmann::json& j, StreamKind value);
void from_json(const nlohmann::json& j, StreamKind& value) noexcept;

void to_json(nlohmann::json& j, const HubStream& value);
void from_json(const nlohmann::json& j, HubStream& value) noexcept;
void to_json(nlohmann::json& j, const HubAttempt& value);
void from_json(const nlohmann::json& j, HubAttempt& value) noexcept;
void to_json(nlohmann::json& j, const HubInstance& value);
void from_json(const nlohmann::json& j, HubInstance& value) noexcept;
void to_json(nlohmann::json& j, const HubApp& value);
void from_json(const nlohmann::json& j, HubApp& value) noexcept;
void to_json(nlohmann::json& j, const HubSuite& value);
void from_json(const nlohmann::json& j, HubSuite& value) noexcept;
void to_json(nlohmann::json& j, const HubRun& value);
void from_json(const nlohmann::json& j, HubRun& value) noexcept;
void to_json(nlohmann::json& j, const HubTree& value);
void from_json(const nlohmann::json& j, HubTree& value) noexcept;
void to_json(nlohmann::json& j, const StreamRecord& value);
void from_json(const nlohmann::json& j, StreamRecord& value) noexcept;
void to_json(nlohmann::json& j, const GapMarker& value);
void from_json(const nlohmann::json& j, GapMarker& value) noexcept;

/// Flatten all streams in a tree (including suite runner logs).
[[nodiscard]] std::vector<HubStream> all_streams(const HubTree& tree);
/// Find one stream by id, or nullptr.
[[nodiscard]] const HubStream* find_stream(const HubTree& tree,
                                            std::string_view id) noexcept;

} // namespace nexigen::hub

template <>
struct std::formatter<nexigen::hub::StreamKind> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::hub::StreamKind value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::hub::to_string(value), ctx);
    }
};

#define NEXIGEN_HUB_FORMATTER(Type)                                                        \
    template <>                                                                            \
    struct std::formatter<nexigen::hub::Type> : std::formatter<std::string_view> {         \
        template <class Ctx>                                                               \
        constexpr auto format(const nexigen::hub::Type& value, Ctx& ctx) const {           \
            return std::formatter<std::string_view>::format(value.to_string(), ctx);       \
        }                                                                                  \
    }

NEXIGEN_HUB_FORMATTER(HubStream);
NEXIGEN_HUB_FORMATTER(HubAttempt);
NEXIGEN_HUB_FORMATTER(HubInstance);
NEXIGEN_HUB_FORMATTER(HubApp);
NEXIGEN_HUB_FORMATTER(HubSuite);
NEXIGEN_HUB_FORMATTER(HubRun);
NEXIGEN_HUB_FORMATTER(HubTree);
NEXIGEN_HUB_FORMATTER(StreamRecord);
NEXIGEN_HUB_FORMATTER(GapMarker);

#undef NEXIGEN_HUB_FORMATTER
