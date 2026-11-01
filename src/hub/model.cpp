// nexigen::hub — read-only observatory model
// (change nexigen-hub; specs hub/discovery, hub/streaming).
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/model.hpp>

namespace nexigen::hub {

namespace {

[[nodiscard]] bool read_string(const nlohmann::json& document,
                               const char* key, std::string& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_string()) {
        return false;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return false;
    }
    target = *text;
    return true;
}

[[nodiscard]] bool read_u32(const nlohmann::json& document, const char* key,
                            std::uint32_t& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_unsigned()) {
        return false;
    }
    const std::uint64_t value = it->get<std::uint64_t>();
    if (value > UINT32_MAX) {
        return false;
    }
    target = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] bool read_u64(const nlohmann::json& document, const char* key,
                            std::uint64_t& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_unsigned()) {
        return false;
    }
    target = it->get<std::uint64_t>();
    return true;
}

[[nodiscard]] bool read_bool(const nlohmann::json& document, const char* key,
                             bool& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_boolean()) {
        return false;
    }
    target = it->get<bool>();
    return true;
}

template <class Enum>
void read_enum(const nlohmann::json& document, const char* key,
               Enum& target) noexcept {
    if (!document.is_object()) {
        return;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_string()) {
        return;
    }
    const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
    if (name == nullptr) {
        return;
    }
    Enum parsed = target;
    from_json(nlohmann::json(*name), parsed);
    target = parsed;
}

} // namespace

std::string Hub_stream::to_string() const {
    return std::format("Hub_stream(id={}, kind={}, path={})", id,
                       nexigen::hub::to_string(kind), path);
}

std::string Hub_attempt::to_string() const {
    return std::format("Hub_attempt(attempt={}, artifact_dir={}, streams={})",
                       attempt, artifact_dir, streams.size());
}

std::string Hub_instance::to_string() const {
    return std::format(
        "Hub_instance(instance={}, instance_dir={}, state={}, attempts={})",
        instance, instance_dir, nexigen::runner::to_string(state),
        attempts.size());
}

std::string Hub_app::to_string() const {
    return std::format("Hub_app(app={}, state={}, instances={})", app,
                       nexigen::runner::to_string(state), instances.size());
}

std::string Hub_suite::to_string() const {
    return std::format(
        "Hub_suite(suite_id={}, suite_dir={}, artifacts_path={}, state={}, "
        "suite_restarts_used={}, apps={})",
        suite_id, suite_dir, artifacts_path, nexigen::runner::to_string(state),
        suite_restarts_used, apps.size());
}

std::string Hub_run::to_string() const {
    return std::format(
        "Hub_run(run_key={}, run_dir={}, suites={}, standalone_instances={})",
        run_key, run_dir, suites.size(), standalone_instances.size());
}

std::string Hub_tree::to_string() const {
    return std::format("Hub_tree(runs={})", runs.size());
}

std::string Stream_record::to_string() const {
    return std::format(
        "Stream_record(stream_id={}, kind={}, sequence={}, line_size={}, "
        "truncated={})",
        stream_id, nexigen::hub::to_string(kind), sequence, line.size(),
        truncated);
}

std::string Gap_marker::to_string() const {
    return std::format(
        "Gap_marker(stream_id={}, dropped={}, first_missing_sequence={}, "
        "last_missing_sequence={})",
        stream_id, dropped, first_missing_sequence, last_missing_sequence);
}

// --- enum JSON ---
void to_json(nlohmann::json& j, Stream_kind value) {
    j = std::string(to_string(value));
}

void from_json(const nlohmann::json& j, Stream_kind& value) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), value));
    }
}

// --- struct JSON ---
void to_json(nlohmann::json& j, const Hub_stream& value) {
    j = nlohmann::json{{"id", value.id},
                       {"kind", std::string(to_string(value.kind))},
                       {"path", value.path}};
}

void from_json(const nlohmann::json& j, Hub_stream& value) noexcept {
    static_cast<void>(read_string(j, "id", value.id));
    read_enum(j, "kind", value.kind);
    static_cast<void>(read_string(j, "path", value.path));
}

void to_json(nlohmann::json& j, const Hub_attempt& value) {
    nlohmann::json streams = nlohmann::json::array();
    for (const Hub_stream& stream : value.streams) {
        nlohmann::json entry;
        to_json(entry, stream);
        streams.push_back(std::move(entry));
    }
    j = nlohmann::json{{"attempt", value.attempt},
                       {"artifact_dir", value.artifact_dir},
                       {"streams", std::move(streams)}};
}

void from_json(const nlohmann::json& j, Hub_attempt& value) noexcept {
    static_cast<void>(read_u32(j, "attempt", value.attempt));
    static_cast<void>(read_string(j, "artifact_dir", value.artifact_dir));
    if (const auto it = j.find("streams"); it != j.end() && it->is_array()) {
        std::vector<Hub_stream> streams;
        streams.reserve(it->size());
        for (const auto& entry : *it) {
            Hub_stream stream;
            from_json(entry, stream);
            streams.push_back(std::move(stream));
        }
        value.streams = std::move(streams);
    }
}

void to_json(nlohmann::json& j, const Hub_instance& value) {
    nlohmann::json attempts = nlohmann::json::array();
    for (const Hub_attempt& attempt : value.attempts) {
        nlohmann::json entry;
        to_json(entry, attempt);
        attempts.push_back(std::move(entry));
    }
    j = nlohmann::json{{"instance", value.instance},
                       {"instance_dir", value.instance_dir},
                       {"state", std::string(nexigen::runner::to_string(value.state))},
                       {"attempts", std::move(attempts)}};
}

void from_json(const nlohmann::json& j, Hub_instance& value) noexcept {
    static_cast<void>(read_string(j, "instance", value.instance));
    static_cast<void>(read_string(j, "instance_dir", value.instance_dir));
    read_enum(j, "state", value.state);
    if (const auto it = j.find("attempts"); it != j.end() && it->is_array()) {
        std::vector<Hub_attempt> attempts;
        attempts.reserve(it->size());
        for (const auto& entry : *it) {
            Hub_attempt attempt;
            from_json(entry, attempt);
            attempts.push_back(std::move(attempt));
        }
        value.attempts = std::move(attempts);
    }
}

void to_json(nlohmann::json& j, const Hub_app& value) {
    nlohmann::json instances = nlohmann::json::array();
    for (const Hub_instance& instance : value.instances) {
        nlohmann::json entry;
        to_json(entry, instance);
        instances.push_back(std::move(entry));
    }
    j = nlohmann::json{{"app", value.app},
                       {"state", std::string(nexigen::runner::to_string(value.state))},
                       {"instances", std::move(instances)}};
}

void from_json(const nlohmann::json& j, Hub_app& value) noexcept {
    static_cast<void>(read_string(j, "app", value.app));
    read_enum(j, "state", value.state);
    if (const auto it = j.find("instances"); it != j.end() && it->is_array()) {
        std::vector<Hub_instance> instances;
        instances.reserve(it->size());
        for (const auto& entry : *it) {
            Hub_instance instance;
            from_json(entry, instance);
            instances.push_back(std::move(instance));
        }
        value.instances = std::move(instances);
    }
}

void to_json(nlohmann::json& j, const Hub_suite& value) {
    nlohmann::json apps = nlohmann::json::array();
    for (const Hub_app& app : value.apps) {
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

void from_json(const nlohmann::json& j, Hub_suite& value) noexcept {
    static_cast<void>(read_string(j, "suite_id", value.suite_id));
    static_cast<void>(read_string(j, "suite_dir", value.suite_dir));
    static_cast<void>(read_string(j, "artifacts_path", value.artifacts_path));
    read_enum(j, "state", value.state);
    static_cast<void>(read_u32(j, "suite_restarts_used",
                               value.suite_restarts_used));
    static_cast<void>(read_string(j, "reason", value.reason));
    if (const auto it = j.find("runner_log"); it != j.end()) {
        from_json(*it, value.runner_log);
    }
    if (const auto it = j.find("apps"); it != j.end() && it->is_array()) {
        std::vector<Hub_app> apps;
        apps.reserve(it->size());
        for (const auto& entry : *it) {
            Hub_app app;
            from_json(entry, app);
            apps.push_back(std::move(app));
        }
        value.apps = std::move(apps);
    }
}

void to_json(nlohmann::json& j, const Hub_run& value) {
    nlohmann::json suites = nlohmann::json::array();
    for (const Hub_suite& suite : value.suites) {
        nlohmann::json entry;
        to_json(entry, suite);
        suites.push_back(std::move(entry));
    }
    nlohmann::json standalone = nlohmann::json::array();
    for (const Hub_instance& instance : value.standalone_instances) {
        nlohmann::json entry;
        to_json(entry, instance);
        standalone.push_back(std::move(entry));
    }
    j = nlohmann::json{{"run_key", value.run_key},
                       {"run_dir", value.run_dir},
                       {"suites", std::move(suites)},
                       {"standalone_instances", std::move(standalone)}};
}

void from_json(const nlohmann::json& j, Hub_run& value) noexcept {
    static_cast<void>(read_string(j, "run_key", value.run_key));
    static_cast<void>(read_string(j, "run_dir", value.run_dir));
    if (const auto it = j.find("suites"); it != j.end() && it->is_array()) {
        std::vector<Hub_suite> suites;
        suites.reserve(it->size());
        for (const auto& entry : *it) {
            Hub_suite suite;
            from_json(entry, suite);
            suites.push_back(std::move(suite));
        }
        value.suites = std::move(suites);
    }
    if (const auto it = j.find("standalone_instances");
        it != j.end() && it->is_array()) {
        std::vector<Hub_instance> instances;
        instances.reserve(it->size());
        for (const auto& entry : *it) {
            Hub_instance instance;
            from_json(entry, instance);
            instances.push_back(std::move(instance));
        }
        value.standalone_instances = std::move(instances);
    }
}

void to_json(nlohmann::json& j, const Hub_tree& value) {
    nlohmann::json runs = nlohmann::json::array();
    for (const Hub_run& run : value.runs) {
        nlohmann::json entry;
        to_json(entry, run);
        runs.push_back(std::move(entry));
    }
    j = nlohmann::json{{"runs", std::move(runs)}};
}

void from_json(const nlohmann::json& j, Hub_tree& value) noexcept {
    if (const auto it = j.find("runs"); it != j.end() && it->is_array()) {
        std::vector<Hub_run> runs;
        runs.reserve(it->size());
        for (const auto& entry : *it) {
            Hub_run run;
            from_json(entry, run);
            runs.push_back(std::move(run));
        }
        value.runs = std::move(runs);
    }
}

void to_json(nlohmann::json& j, const Stream_record& value) {
    j = nlohmann::json{{"stream_id", value.stream_id},
                       {"kind", std::string(to_string(value.kind))},
                       {"sequence", value.sequence},
                       {"received_at", value.received_at_rfc3339},
                       {"line", value.line},
                       {"truncated", value.truncated}};
}

void from_json(const nlohmann::json& j, Stream_record& value) noexcept {
    static_cast<void>(read_string(j, "stream_id", value.stream_id));
    read_enum(j, "kind", value.kind);
    static_cast<void>(read_u64(j, "sequence", value.sequence));
    static_cast<void>(read_string(j, "received_at", value.received_at_rfc3339));
    static_cast<void>(read_string(j, "line", value.line));
    static_cast<void>(read_bool(j, "truncated", value.truncated));
}

void to_json(nlohmann::json& j, const Gap_marker& value) {
    j = nlohmann::json{{"stream_id", value.stream_id},
                       {"dropped", value.dropped},
                       {"first_missing_sequence", value.first_missing_sequence},
                       {"last_missing_sequence", value.last_missing_sequence}};
}

void from_json(const nlohmann::json& j, Gap_marker& value) noexcept {
    static_cast<void>(read_string(j, "stream_id", value.stream_id));
    static_cast<void>(read_u64(j, "dropped", value.dropped));
    static_cast<void>(read_u64(j, "first_missing_sequence",
                               value.first_missing_sequence));
    static_cast<void>(read_u64(j, "last_missing_sequence",
                               value.last_missing_sequence));
}

std::vector<Hub_stream> all_streams(const Hub_tree& tree) {
    std::vector<Hub_stream> streams;
    const auto append_instance = [&](const Hub_instance& instance) {
        for (const Hub_attempt& attempt : instance.attempts) {
            for (const Hub_stream& stream : attempt.streams) {
                streams.push_back(stream);
            }
        }
    };
    for (const Hub_run& run : tree.runs) {
        for (const Hub_suite& suite : run.suites) {
            if (!suite.runner_log.id.empty()) {
                streams.push_back(suite.runner_log);
            }
            for (const Hub_app& app : suite.apps) {
                for (const Hub_instance& instance : app.instances) {
                    append_instance(instance);
                }
            }
        }
        for (const Hub_instance& instance : run.standalone_instances) {
            append_instance(instance);
        }
    }
    return streams;
}

const Hub_stream* find_stream(const Hub_tree& tree, std::string_view id) noexcept {
    for (const Hub_run& run : tree.runs) {
        for (const Hub_suite& suite : run.suites) {
            if (suite.runner_log.id == id) {
                return &suite.runner_log;
            }
            for (const Hub_app& app : suite.apps) {
                for (const Hub_instance& instance : app.instances) {
                    for (const Hub_attempt& attempt : instance.attempts) {
                        for (const Hub_stream& stream : attempt.streams) {
                            if (stream.id == id) {
                                return &stream;
                            }
                        }
                    }
                }
            }
        }
        for (const Hub_instance& instance : run.standalone_instances) {
            for (const Hub_attempt& attempt : instance.attempts) {
                for (const Hub_stream& stream : attempt.streams) {
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
