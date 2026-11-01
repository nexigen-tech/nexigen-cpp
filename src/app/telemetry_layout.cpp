// nexigen::app — cold-path identity and telemetry layout implementation
// (change add-telemetry-output-contract; spec app/telemetry-layout).
//
// Initialization-time only: getenv, std::string, and JSON are allowed here and
// never reach a producer, tick, snapshot, or sink path.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <nexigen/app/telemetry_layout.hpp>

#include <cstdlib>
#include <string>
#include <string_view>

namespace nexigen::app {
namespace {

[[nodiscard]] std::string_view env_value(const char* name) noexcept {
    const char* const value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

[[nodiscard]] bool safe_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 128U) {
        return false;
    }
    if (id == "." || id == ".." || id.find("..") != std::string_view::npos) {
        return false;
    }
    for (const char c : id) {
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                             (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                             c == '-';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool absolute_path(std::string_view path) noexcept {
    return !path.empty() && path.front() == '/';
}

[[nodiscard]] std::string derive_instance_dir(std::string_view run_dir,
                                              std::string_view app_name,
                                              std::string_view instance_id) {
    std::string out{run_dir};
    while (!out.empty() && out.back() == '/') {
        out.pop_back();
    }
    out += '/';
    out += app_name;
    out += '/';
    out += instance_id;
    return out;
}

[[nodiscard]] std::string choose_path(const RunIdentity& identity,
                                      std::string_view explicit_path,
                                      std::string_view filename) {
    if (!explicit_path.empty()) {
        return std::string(explicit_path);
    }
    if (!identity.instance_dir.empty()) {
        std::string out{identity.instance_dir};
        while (!out.empty() && out.back() == '/') {
            out.pop_back();
        }
        out += '/';
        out += filename;
        return out;
    }
    return {};
}

void read_string_field(const nlohmann::json& j, const char* key,
                       std::string& target) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) {
        return;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text != nullptr) {
        target = std::string(*text);
    }
}

} // namespace

std::string RunIdentity::to_string() const {
    return "RunIdentity(run_id=" + run_id + ", run_dir=" + run_dir +
           ", app_name=" + app_name + ", instance_id=" + instance_id +
           ", instance_dir=" + instance_dir + ")";
}

std::string TelemetryPaths::to_string() const {
    return "TelemetryPaths(metrics=" + metrics + ", traces=" + traces +
           ", chrono=" + chrono + ")";
}

RunIdentity load_run_identity() noexcept {
    RunIdentity identity;
    identity.run_id = std::string(env_value("NEXIGEN_RUN_ID"));
    identity.run_dir = std::string(env_value("NEXIGEN_RUN_DIR"));
    identity.app_name = std::string(env_value("NEXIGEN_APP_NAME"));
    identity.instance_id = std::string(env_value("NEXIGEN_INSTANCE_ID"));
    identity.instance_dir = std::string(env_value("NEXIGEN_INSTANCE_DIR"));
    if (identity.instance_dir.empty() && !identity.run_dir.empty() &&
        !identity.app_name.empty() && !identity.instance_id.empty()) {
        identity.instance_dir =
            derive_instance_dir(identity.run_dir, identity.app_name, identity.instance_id);
    }
    return identity;
}

core::Status validate_run_identity(const RunIdentity& identity) noexcept {
    if (!identity.run_id.empty() && !safe_id(identity.run_id)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!identity.app_name.empty() && !safe_id(identity.app_name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!identity.instance_id.empty() && !safe_id(identity.instance_id)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!identity.run_dir.empty() && !absolute_path(identity.run_dir)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!identity.instance_dir.empty() && !absolute_path(identity.instance_dir)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

core::Status resolve_telemetry_paths(const RunIdentity& identity,
                                     std::string_view explicit_metrics_path,
                                     std::string_view explicit_traces_path,
                                     std::string_view explicit_chrono_path,
                                     TelemetryPaths& out) noexcept {
    const core::Status valid = validate_run_identity(identity);
    if (!valid) {
        return valid;
    }
    TelemetryPaths resolved{
        choose_path(identity, explicit_metrics_path, "metrics.jsonl"),
        choose_path(identity, explicit_traces_path, "traces.jsonl"),
        choose_path(identity, explicit_chrono_path, "chrono.jsonl")};
    out = resolved;
    return {};
}

void to_json(nlohmann::json& j, const RunIdentity& identity) {
    j = nlohmann::json{{"run_id", identity.run_id},
                       {"run_dir", identity.run_dir},
                       {"app_name", identity.app_name},
                       {"instance_id", identity.instance_id},
                       {"instance_dir", identity.instance_dir}};
}

void from_json(const nlohmann::json& j, RunIdentity& identity) noexcept {
    read_string_field(j, "run_id", identity.run_id);
    read_string_field(j, "run_dir", identity.run_dir);
    read_string_field(j, "app_name", identity.app_name);
    read_string_field(j, "instance_id", identity.instance_id);
    read_string_field(j, "instance_dir", identity.instance_dir);
}

void to_json(nlohmann::json& j, const TelemetryPaths& paths) {
    j = nlohmann::json{{"metrics", paths.metrics},
                       {"traces", paths.traces},
                       {"chrono", paths.chrono}};
}

void from_json(const nlohmann::json& j, TelemetryPaths& paths) noexcept {
    read_string_field(j, "metrics", paths.metrics);
    read_string_field(j, "traces", paths.traces);
    read_string_field(j, "chrono", paths.chrono);
}

} // namespace nexigen::app
