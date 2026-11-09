// nexigen::exec — suite reporting: metadata, state, exit, status derivation
// (change refactor-app-suite-layers; spec execution_manager/app-suite).
//
// COLD PATH ONLY. Owns every suite JSON record and the read-side status merge.
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/exec/app_state.hpp>
#include <nexigen/exec/detail/path_utils.hpp>
#include <nexigen/exec/detail/suite_reporting.hpp>
#include <nexigen/exec/detail/sys_helpers.hpp>
#include <nexigen/exec/instance_file_io.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/suite_app_metadata.hpp>
#include <nexigen/exec/suite_app_status.hpp>
#include <nexigen/exec/suite_exit.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/exec/suite_metadata.hpp>
#include <nexigen/exec/suite_state.hpp>
#include <nexigen/exec/suite_status.hpp>

namespace nexigen::exec::detail {

namespace {

[[nodiscard]] bool load_suite_status_metadata(const std::string& metadata_path,
                                              SuiteMetadata& out) noexcept {
    return read_json_into(metadata_path, out);
}

[[nodiscard]] SuiteState derive_suite_state(const std::string& exit_path,
                                            const SuiteStatus& recorded,
                                            bool have_recorded) noexcept {
    SuiteExit exit{};
    if (read_json_into(exit_path, exit)) {
        return exit.state;
    }
    if (have_recorded) {
        return recorded.state;
    }
    return SuiteState::Unknown;
}

[[nodiscard]] AppState live_status_to_app_state(std::string_view state, AppState recorded,
                                                bool have_recorded) noexcept {
    if (state == "running") {
        return AppState::Running;
    }
    if (state == "stopped") {
        return AppState::Stopped;
    }
    if (state == "exited") {
        return AppState::Exited;
    }
    if (!have_recorded || recorded == AppState::Unknown) {
        return AppState::Unknown;
    }
    return recorded;
}

[[nodiscard]] SuiteAppStatus merge_app_status(const SuiteAppMetadata& app,
                                              const SuiteStatus& recorded,
                                              bool have_recorded) noexcept {
    SuiteAppStatus entry{};
    entry.app = app.app;
    entry.instance = app.instance;
    entry.instance_dir = app.instance_dir;
    for (const SuiteAppStatus& recorded_app : recorded.apps) {
        if (recorded_app.app == app.app && recorded_app.instance == app.instance) {
            entry = recorded_app;
            break;
        }
    }
    const bool sticky = entry.state == AppState::Failed || entry.state == AppState::Restarting;
    if (!sticky) {
        std::string state;
        ExecutionManager execution_manager;
        if (execution_manager.status(app.instance_dir, state).has_value()) {
            entry.state = live_status_to_app_state(state, entry.state, have_recorded);
        }
    }
    return entry;
}

} // namespace

SuiteMetadata build_suite_metadata(const SuiteManifest& manifest, std::int32_t pid,
                                   std::uint64_t start_mono_ns) {
    SuiteMetadata metadata{};
    metadata.suite_id = manifest.suite_id;
    metadata.execution_manager_name = manifest.execution_manager_name;
    metadata.run_id = manifest.run_id;
    metadata.run_dir = manifest.run_dir;
    metadata.global_settings_path = manifest.global_settings_path;
    metadata.artifacts_path = manifest.artifacts_path;
    metadata.pid = pid;
    metadata.start_time_rfc3339 = utc_now_rfc3339();
    metadata.start_time_mono_ns = start_mono_ns;
    // Identity anchor for suite stop (PID-recycling protection): the
    // supervisor records its own starttime ticks while alive right here.
    metadata.proc_start_ticks = detail::proc_start_ticks(pid);
    metadata.manifest_path = manifest.manifest_path;
    metadata.execution_manager_control_thread = manifest.execution_manager_control_thread;
    metadata.execution_manager_logger_thread = manifest.execution_manager_logger_thread;
    metadata.apps.reserve(manifest.apps.size());
    for (const AppSpec& app : manifest.apps) {
        SuiteAppMetadata entry{};
        entry.app = app.app;
        entry.instance = app.instance;
        entry.instance_dir = instance_dir_path(manifest.run_dir, app.app, app.instance);
        entry.stop_grace_s = app.stop_grace_s;
        metadata.apps.push_back(std::move(entry));
    }
    return metadata;
}

bool write_suite_metadata_file(const SuiteMetadata& metadata, std::string_view suite_dir) {
    nlohmann::json metadata_json;
    to_json(metadata_json, metadata);
    return write_json_file(join_path(suite_dir, "suite.json"), metadata_json).has_value();
}

void write_suite_status_file(const SuiteStatus& status, std::string_view state_path) noexcept {
    nlohmann::json document;
    to_json(document, status);
    static_cast<void>(write_json_file(state_path, document));
}

void write_suite_exit_file(const SuiteExit& exit, std::string_view exit_path) noexcept {
    nlohmann::json document;
    to_json(document, exit);
    static_cast<void>(write_json_file(exit_path, document));
}

void write_terminal_suite_state(const SuiteMetadata& metadata, std::string_view state_path,
                                std::string_view exit_path) {
    SuiteStatus status{};
    if (!read_json_into(state_path, status)) {
        status.suite_id = metadata.suite_id;
        status.run_dir = metadata.run_dir;
        status.artifacts_path = metadata.artifacts_path;
        status.apps.reserve(metadata.apps.size());
        for (const SuiteAppMetadata& app : metadata.apps) {
            SuiteAppStatus entry{};
            entry.app = app.app;
            entry.instance = app.instance;
            entry.instance_dir = app.instance_dir;
            entry.state = AppState::Stopped;
            status.apps.push_back(std::move(entry));
        }
    }
    status.state = SuiteState::Stopped;
    for (SuiteAppStatus& app : status.apps) {
        app.state = AppState::Stopped;
    }
    write_suite_status_file(status, state_path);

    SuiteExit exit{};
    exit.state = SuiteState::Stopped;
    exit.reason = "forced_stop";
    write_suite_exit_file(exit, exit_path);
}

} // namespace nexigen::exec::detail

namespace nexigen::exec {

core::Status suite_status(std::string_view suite_dir, SuiteStatus& out) noexcept {
    out = SuiteStatus{};
    if (suite_dir.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const std::string directory(suite_dir);
    const std::string metadata_path = detail::join_path(directory, "suite.json");
    const std::string state_path = detail::join_path(directory, "suite-state.json");
    const std::string exit_path = detail::join_path(directory, "suite-exit.json");

    SuiteMetadata metadata{};
    if (!detail::load_suite_status_metadata(metadata_path, metadata)) {
        return {};
    }

    out.suite_id = metadata.suite_id;
    out.run_dir = metadata.run_dir;
    out.artifacts_path = metadata.artifacts_path;

    SuiteStatus recorded{};
    const bool have_recorded = read_json_into(state_path, recorded);

    out.state = detail::derive_suite_state(exit_path, recorded, have_recorded);
    if (have_recorded) {
        out.suite_restarts_used = recorded.suite_restarts_used;
    }

    out.apps.reserve(metadata.apps.size());
    for (const SuiteAppMetadata& app : metadata.apps) {
        out.apps.push_back(detail::merge_app_status(app, recorded, have_recorded));
    }
    return {};
}

} // namespace nexigen::exec
