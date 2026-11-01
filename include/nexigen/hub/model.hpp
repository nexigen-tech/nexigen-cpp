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
enum class Stream_kind : std::uint8_t {
    App,
    Metrics,
    Traces,
    Chrono,
    Stdout,
    Stderr,
    Runner,
};

[[nodiscard]] constexpr std::string_view to_string(Stream_kind kind) noexcept {
    switch (kind) {
        case Stream_kind::App:     return "app";
        case Stream_kind::Metrics: return "metrics";
        case Stream_kind::Traces:  return "traces";
        case Stream_kind::Chrono:  return "chrono";
        case Stream_kind::Stdout:  return "stdout";
        case Stream_kind::Stderr:  return "stderr";
        case Stream_kind::Runner:  return "runner";
        default:                   return "unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         Stream_kind& out) noexcept {
    if (name == "app")     { out = Stream_kind::App;     return true; }
    if (name == "metrics") { out = Stream_kind::Metrics; return true; }
    if (name == "traces")  { out = Stream_kind::Traces;  return true; }
    if (name == "chrono")  { out = Stream_kind::Chrono;  return true; }
    if (name == "stdout")  { out = Stream_kind::Stdout;  return true; }
    if (name == "stderr")  { out = Stream_kind::Stderr;  return true; }
    if (name == "runner")  { out = Stream_kind::Runner;  return true; }
    return false;
}

/// One tailed file.
struct Hub_stream {
    std::string id{};
    Stream_kind kind{Stream_kind::App};
    std::string path{};

    [[nodiscard]] std::string to_string() const;
};

/// One launch attempt and its streams.
struct Hub_attempt {
    std::uint32_t attempt{0};
    std::string artifact_dir{};
    std::vector<Hub_stream> streams{};

    [[nodiscard]] std::string to_string() const;
};

/// One application instance (control directory + attempts).
struct Hub_instance {
    std::string instance{};
    std::string instance_dir{};
    runner::App_state state{runner::App_state::Unknown};
    std::vector<Hub_attempt> attempts{};

    [[nodiscard]] std::string to_string() const;
};

/// One application and all of its instances.
struct Hub_app {
    std::string app{};
    runner::App_state state{runner::App_state::Unknown};
    std::vector<Hub_instance> instances{};

    [[nodiscard]] std::string to_string() const;
};

/// One suite and its applications.
struct Hub_suite {
    std::string suite_id{};
    std::string suite_dir{};
    std::string artifacts_path{};
    runner::Suite_state state{runner::Suite_state::Unknown};
    std::uint32_t suite_restarts_used{0};
    std::string reason{};
    Hub_stream runner_log{};
    std::vector<Hub_app> apps{};

    [[nodiscard]] std::string to_string() const;
};

/// One run root.
struct Hub_run {
    std::string run_key{};
    std::string run_dir{};
    std::vector<Hub_suite> suites{};
    std::vector<Hub_instance> standalone_instances{};

    [[nodiscard]] std::string to_string() const;
};

/// Complete discovered tree.
struct Hub_tree {
    std::vector<Hub_run> runs{};

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] bool empty() const noexcept { return runs.empty(); }
};

/// One complete line from a stream.
struct Stream_record {
    std::string stream_id{};
    Stream_kind kind{Stream_kind::App};
    std::uint64_t sequence{0};
    std::string received_at_rfc3339{};
    std::string line{};
    bool truncated{false};

    [[nodiscard]] std::string to_string() const;
};

/// A missing sequence range in a stream ring.
struct Gap_marker {
    std::string stream_id{};
    std::uint64_t dropped{0};
    std::uint64_t first_missing_sequence{0};
    std::uint64_t last_missing_sequence{0};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helpers
[[nodiscard]] inline std::string to_string(const Hub_stream& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Hub_attempt& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Hub_instance& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Hub_app& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Hub_suite& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Hub_run& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Hub_tree& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Stream_record& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Gap_marker& value) { return value.to_string(); }

// --- cold-path ADL serialization (bodies in src/hub/model.cpp)
void to_json(nlohmann::json& j, Stream_kind value);
void from_json(const nlohmann::json& j, Stream_kind& value) noexcept;

void to_json(nlohmann::json& j, const Hub_stream& value);
void from_json(const nlohmann::json& j, Hub_stream& value) noexcept;
void to_json(nlohmann::json& j, const Hub_attempt& value);
void from_json(const nlohmann::json& j, Hub_attempt& value) noexcept;
void to_json(nlohmann::json& j, const Hub_instance& value);
void from_json(const nlohmann::json& j, Hub_instance& value) noexcept;
void to_json(nlohmann::json& j, const Hub_app& value);
void from_json(const nlohmann::json& j, Hub_app& value) noexcept;
void to_json(nlohmann::json& j, const Hub_suite& value);
void from_json(const nlohmann::json& j, Hub_suite& value) noexcept;
void to_json(nlohmann::json& j, const Hub_run& value);
void from_json(const nlohmann::json& j, Hub_run& value) noexcept;
void to_json(nlohmann::json& j, const Hub_tree& value);
void from_json(const nlohmann::json& j, Hub_tree& value) noexcept;
void to_json(nlohmann::json& j, const Stream_record& value);
void from_json(const nlohmann::json& j, Stream_record& value) noexcept;
void to_json(nlohmann::json& j, const Gap_marker& value);
void from_json(const nlohmann::json& j, Gap_marker& value) noexcept;

/// Flatten all streams in a tree (including suite runner logs).
[[nodiscard]] std::vector<Hub_stream> all_streams(const Hub_tree& tree);
/// Find one stream by id, or nullptr.
[[nodiscard]] const Hub_stream* find_stream(const Hub_tree& tree,
                                            std::string_view id) noexcept;

} // namespace nexigen::hub

template <>
struct std::formatter<nexigen::hub::Stream_kind> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::hub::Stream_kind value, Ctx& ctx) const {
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

NEXIGEN_HUB_FORMATTER(Hub_stream);
NEXIGEN_HUB_FORMATTER(Hub_attempt);
NEXIGEN_HUB_FORMATTER(Hub_instance);
NEXIGEN_HUB_FORMATTER(Hub_app);
NEXIGEN_HUB_FORMATTER(Hub_suite);
NEXIGEN_HUB_FORMATTER(Hub_run);
NEXIGEN_HUB_FORMATTER(Hub_tree);
NEXIGEN_HUB_FORMATTER(Stream_record);
NEXIGEN_HUB_FORMATTER(Gap_marker);

#undef NEXIGEN_HUB_FORMATTER
