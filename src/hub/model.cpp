// nexigen::hub — read-only observatory model
// (change nexigen-hub; specs hub/discovery, hub/streaming).
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/hub/model.hpp>
#include <nexigen/runner/detail/sys_helpers.hpp>

namespace nexigen::hub {

using nexigen::core::mandate::enum_from_json;
using nexigen::core::mandate::enum_to_json;
using nexigen::runner::detail::read_bool;
using nexigen::core::mandate::read_enum;
using nexigen::runner::detail::read_string;
using nexigen::runner::detail::read_u32;
using nexigen::runner::detail::read_u64;

std::string HubStream::to_string() const {
    return std::format("HubStream(id={}, kind={}, path={})", id, nexigen::hub::to_string(kind),
                       path);
}

std::string HubAttempt::to_string() const {
    return std::format("HubAttempt(attempt={}, artifact_dir={}, streams={})", attempt, artifact_dir,
                       streams.size());
}

std::string HubInstance::to_string() const {
    return std::format("HubInstance(instance={}, instance_dir={}, state={}, attempts={})", instance,
                       instance_dir, nexigen::runner::to_string(state), attempts.size());
}

std::string HubApp::to_string() const {
    return std::format("HubApp(app={}, state={}, instances={})", app,
                       nexigen::runner::to_string(state), instances.size());
}

std::string HubSuite::to_string() const {
    return std::format("HubSuite(suite_id={}, suite_dir={}, artifacts_path={}, state={}, "
                       "suite_restarts_used={}, apps={})",
                       suite_id, suite_dir, artifacts_path, nexigen::runner::to_string(state),
                       suite_restarts_used, apps.size());
}

std::string HubRun::to_string() const {
    return std::format("HubRun(run_key={}, run_dir={}, suites={}, standalone_instances={})",
                       run_key, run_dir, suites.size(), standalone_instances.size());
}

std::string HubTree::to_string() const {
    return std::format("HubTree(runs={})", runs.size());
}

std::string StreamRecord::to_string() const {
    return std::format("StreamRecord(stream_id={}, kind={}, sequence={}, line_size={}, "
                       "truncated={})",
                       stream_id, nexigen::hub::to_string(kind), sequence, line.size(), truncated);
}

std::string GapMarker::to_string() const {
    return std::format("GapMarker(stream_id={}, dropped={}, first_missing_sequence={}, "
                       "last_missing_sequence={})",
                       stream_id, dropped, first_missing_sequence, last_missing_sequence);
}

// --- enum JSON ---
void to_json(nlohmann::json& j, StreamKind value) {
    enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, StreamKind& value) noexcept {
    enum_from_json(j, value);
}

// --- struct JSON ---
void to_json(nlohmann::json& j, const HubStream& value) {
    j = nlohmann::json{
        {"id", value.id}, {"kind", std::string(to_string(value.kind))}, {"path", value.path}};
}

void from_json(const nlohmann::json& j, HubStream& value) noexcept {
    static_cast<void>(read_string(j, "id", value.id));
    read_enum(j, "kind", value.kind);
    static_cast<void>(read_string(j, "path", value.path));
}

void to_json(nlohmann::json& j, const HubAttempt& value) {
    nlohmann::json streams = nlohmann::json::array();
    for (const HubStream& stream : value.streams) {
        nlohmann::json entry;
        to_json(entry, stream);
        streams.push_back(std::move(entry));
    }
    j = nlohmann::json{{"attempt", value.attempt},
                       {"artifact_dir", value.artifact_dir},
                       {"streams", std::move(streams)}};
}

void from_json(const nlohmann::json& j, HubAttempt& value) noexcept {
    static_cast<void>(read_u32(j, "attempt", value.attempt));
    static_cast<void>(read_string(j, "artifact_dir", value.artifact_dir));
    if (const auto it = j.find("streams"); it != j.end() && it->is_array()) {
        std::vector<HubStream> streams;
        streams.reserve(it->size());
        for (const auto& entry : *it) {
            HubStream stream;
            from_json(entry, stream);
            streams.push_back(std::move(stream));
        }
        value.streams = std::move(streams);
    }
}

void to_json(nlohmann::json& j, const HubInstance& value) {
    nlohmann::json attempts = nlohmann::json::array();
    for (const HubAttempt& attempt : value.attempts) {
        nlohmann::json entry;
        to_json(entry, attempt);
        attempts.push_back(std::move(entry));
    }
    j = nlohmann::json{{"instance", value.instance},
                       {"instance_dir", value.instance_dir},
                       {"state", std::string(nexigen::runner::to_string(value.state))},
                       {"attempts", std::move(attempts)}};
}

void from_json(const nlohmann::json& j, HubInstance& value) noexcept {
    static_cast<void>(read_string(j, "instance", value.instance));
    static_cast<void>(read_string(j, "instance_dir", value.instance_dir));
    read_enum(j, "state", value.state);
    if (const auto it = j.find("attempts"); it != j.end() && it->is_array()) {
        std::vector<HubAttempt> attempts;
        attempts.reserve(it->size());
        for (const auto& entry : *it) {
            HubAttempt attempt;
            from_json(entry, attempt);
            attempts.push_back(std::move(attempt));
        }
        value.attempts = std::move(attempts);
    }
}

void to_json(nlohmann::json& j, const HubApp& value) {
    nlohmann::json instances = nlohmann::json::array();
    for (const HubInstance& instance : value.instances) {
        nlohmann::json entry;
        to_json(entry, instance);
        instances.push_back(std::move(entry));
    }
    j = nlohmann::json{{"app", value.app},
                       {"state", std::string(nexigen::runner::to_string(value.state))},
                       {"instances", std::move(instances)}};
}

void from_json(const nlohmann::json& j, HubApp& value) noexcept {
    static_cast<void>(read_string(j, "app", value.app));
    read_enum(j, "state", value.state);
    if (const auto it = j.find("instances"); it != j.end() && it->is_array()) {
        std::vector<HubInstance> instances;
        instances.reserve(it->size());
        for (const auto& entry : *it) {
            HubInstance instance;
            from_json(entry, instance);
            instances.push_back(std::move(instance));
        }
        value.instances = std::move(instances);
    }
}

void to_json(nlohmann::json& j, const HubSuite& value) {
    nlohmann::json apps = nlohmann::json::array();
    for (const HubApp& app : value.apps) {
        nlohmann::json entry;
        to_json(entry, app);
        apps.push_back(std::move(entry));
    }
    nlohmann::json runner_log;
    to_json(runner_log, value.runner_log);
    j = nlohmann::json{
        {"suite_id", value.suite_id},
        {"suite_dir", value.suite_dir},
        {"artifacts_path", value.artifacts_path},
        {"state", std::string(nexigen::runner::to_string(value.state))},
        {"suite_restarts_used", value.suite_restarts_used},
        {"reason", value.reason},
        {"runner_log", std::move(runner_log)},
        {"apps", std::move(apps)},
    };
}

void from_json(const nlohmann::json& j, HubSuite& value) noexcept {
    static_cast<void>(read_string(j, "suite_id", value.suite_id));
    static_cast<void>(read_string(j, "suite_dir", value.suite_dir));
    static_cast<void>(read_string(j, "artifacts_path", value.artifacts_path));
    read_enum(j, "state", value.state);
    static_cast<void>(read_u32(j, "suite_restarts_used", value.suite_restarts_used));
    static_cast<void>(read_string(j, "reason", value.reason));
    if (const auto it = j.find("runner_log"); it != j.end()) {
        from_json(*it, value.runner_log);
    }
    if (const auto it = j.find("apps"); it != j.end() && it->is_array()) {
        std::vector<HubApp> apps;
        apps.reserve(it->size());
        for (const auto& entry : *it) {
            HubApp app;
            from_json(entry, app);
            apps.push_back(std::move(app));
        }
        value.apps = std::move(apps);
    }
}

void to_json(nlohmann::json& j, const HubRun& value) {
    nlohmann::json suites = nlohmann::json::array();
    for (const HubSuite& suite : value.suites) {
        nlohmann::json entry;
        to_json(entry, suite);
        suites.push_back(std::move(entry));
    }
    nlohmann::json standalone = nlohmann::json::array();
    for (const HubInstance& instance : value.standalone_instances) {
        nlohmann::json entry;
        to_json(entry, instance);
        standalone.push_back(std::move(entry));
    }
    j = nlohmann::json{{"run_key", value.run_key},
                       {"run_dir", value.run_dir},
                       {"suites", std::move(suites)},
                       {"standalone_instances", std::move(standalone)}};
}

void from_json(const nlohmann::json& j, HubRun& value) noexcept {
    static_cast<void>(read_string(j, "run_key", value.run_key));
    static_cast<void>(read_string(j, "run_dir", value.run_dir));
    if (const auto it = j.find("suites"); it != j.end() && it->is_array()) {
        std::vector<HubSuite> suites;
        suites.reserve(it->size());
        for (const auto& entry : *it) {
            HubSuite suite;
            from_json(entry, suite);
            suites.push_back(std::move(suite));
        }
        value.suites = std::move(suites);
    }
    if (const auto it = j.find("standalone_instances"); it != j.end() && it->is_array()) {
        std::vector<HubInstance> instances;
        instances.reserve(it->size());
        for (const auto& entry : *it) {
            HubInstance instance;
            from_json(entry, instance);
            instances.push_back(std::move(instance));
        }
        value.standalone_instances = std::move(instances);
    }
}

void to_json(nlohmann::json& j, const HubTree& value) {
    nlohmann::json runs = nlohmann::json::array();
    for (const HubRun& run : value.runs) {
        nlohmann::json entry;
        to_json(entry, run);
        runs.push_back(std::move(entry));
    }
    j = nlohmann::json{{"runs", std::move(runs)}};
}

void from_json(const nlohmann::json& j, HubTree& value) noexcept {
    if (const auto it = j.find("runs"); it != j.end() && it->is_array()) {
        std::vector<HubRun> runs;
        runs.reserve(it->size());
        for (const auto& entry : *it) {
            HubRun run;
            from_json(entry, run);
            runs.push_back(std::move(run));
        }
        value.runs = std::move(runs);
    }
}

void to_json(nlohmann::json& j, const StreamRecord& value) {
    j = nlohmann::json{{"stream_id", value.stream_id}, {"kind", std::string(to_string(value.kind))},
                       {"sequence", value.sequence},   {"received_at", value.received_at_rfc3339},
                       {"line", value.line},           {"truncated", value.truncated}};
}

void from_json(const nlohmann::json& j, StreamRecord& value) noexcept {
    static_cast<void>(read_string(j, "stream_id", value.stream_id));
    read_enum(j, "kind", value.kind);
    static_cast<void>(read_u64(j, "sequence", value.sequence));
    static_cast<void>(read_string(j, "received_at", value.received_at_rfc3339));
    static_cast<void>(read_string(j, "line", value.line));
    static_cast<void>(read_bool(j, "truncated", value.truncated));
}

void to_json(nlohmann::json& j, const GapMarker& value) {
    j = nlohmann::json{{"stream_id", value.stream_id},
                       {"dropped", value.dropped},
                       {"first_missing_sequence", value.first_missing_sequence},
                       {"last_missing_sequence", value.last_missing_sequence}};
}

void from_json(const nlohmann::json& j, GapMarker& value) noexcept {
    static_cast<void>(read_string(j, "stream_id", value.stream_id));
    static_cast<void>(read_u64(j, "dropped", value.dropped));
    static_cast<void>(read_u64(j, "first_missing_sequence", value.first_missing_sequence));
    static_cast<void>(read_u64(j, "last_missing_sequence", value.last_missing_sequence));
}

std::vector<HubStream> all_streams(const HubTree& tree) {
    std::vector<HubStream> streams;
    const auto append_instance = [&](const HubInstance& instance) {
        for (const HubAttempt& attempt : instance.attempts) {
            for (const HubStream& stream : attempt.streams) {
                streams.push_back(stream);
            }
        }
    };
    for (const HubRun& run : tree.runs) {
        for (const HubSuite& suite : run.suites) {
            if (!suite.runner_log.id.empty()) {
                streams.push_back(suite.runner_log);
            }
            for (const HubApp& app : suite.apps) {
                for (const HubInstance& instance : app.instances) {
                    append_instance(instance);
                }
            }
        }
        for (const HubInstance& instance : run.standalone_instances) {
            append_instance(instance);
        }
    }
    return streams;
}

const HubStream* find_stream(const HubTree& tree, std::string_view id) noexcept {
    for (const HubRun& run : tree.runs) {
        for (const HubSuite& suite : run.suites) {
            if (suite.runner_log.id == id) {
                return &suite.runner_log;
            }
            for (const HubApp& app : suite.apps) {
                for (const HubInstance& instance : app.instances) {
                    for (const HubAttempt& attempt : instance.attempts) {
                        for (const HubStream& stream : attempt.streams) {
                            if (stream.id == id) {
                                return &stream;
                            }
                        }
                    }
                }
            }
        }
        for (const HubInstance& instance : run.standalone_instances) {
            for (const HubAttempt& attempt : instance.attempts) {
                for (const HubStream& stream : attempt.streams) {
                    if (stream.id == id) {
                        return &stream;
                    }
                }
            }
        }
    }
    return nullptr;
}

} // namespace nexigen::hub
