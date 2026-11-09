// nexigen::exec — execution_manager-owned logging and CPU affinity
// (change nexigen-exec-logging; spec execution_manager/logging).
#include <cstdint>
#include <cstdio>
#include <format>
#include <memory>
#include <string>
#include <string_view>

#include <sched.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/settings.hpp>
#include <nexigen/exec/detail/cpu_validation.hpp>
#include <nexigen/exec/detail/sys_helpers.hpp>
#include <nexigen/exec/instance_lifecycle.hpp>
#include <nexigen/exec/execution_manager_logging.hpp>

namespace nexigen::exec {

using detail::nanoseconds_per_second;
using detail::sleep_ms;

namespace {

using ExecutionManagerFileLogger = logging::FileLogger<64, 256, 4>;

struct LoggingState {
    std::unique_ptr<ExecutionManagerFileLogger> logger{};
    std::string base_dir{};
    std::string execution_manager_name{"nexigen-exec"};
    logging::Settings settings{};
    core::LogLevel level{core::LogLevel::Info};
    core::ThreadSpec logger_thread{};
    bool configured{false};
};

LoggingState& state() {
    static LoggingState instance{};
    return instance;
}

void warn_stderr(std::string_view message) noexcept {
    std::fprintf(stderr, "nexigen-exec: %.*s\n", static_cast<int>(message.size()), message.data());
}

[[nodiscard]] std::string fallback_directory() {
    char buffer[4096]{};
    if (::getcwd(buffer, sizeof(buffer)) == nullptr) {
        return "/tmp/.nexigen-exec";
    }
    return std::string(buffer) + "/.nexigen-exec";
}

[[nodiscard]] bool has_thread_request(const core::ThreadSpec& spec) noexcept {
    return !spec.cpus().empty() || !spec.name().empty() || spec.scheduler_configured() ||
           spec.apply_mode() == core::ThreadApplyMode::Required;
}

[[nodiscard]] core::Result<core::ThreadSpec>
resolve_logger_thread(const core::ThreadSpec& requested,
                      const logging::Settings& settings) noexcept {
    core::ThreadSpec resolved =
        has_thread_request(requested)
            ? requested
            : (settings.global_loaded ? settings.options.worker_thread : core::ThreadSpec{});
    const core::Status valid = resolved.validate();
    if (!valid) {
        if (resolved.apply_mode() == core::ThreadApplyMode::Required) {
            return std::unexpected(valid.error());
        }
        warn_stderr("logger thread spec is invalid; worker attributes disabled");
        return core::ThreadSpec{};
    }
    if (!detail::cpus_within_online(resolved.cpus())) {
        if (resolved.apply_mode() == core::ThreadApplyMode::Required) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        warn_stderr("logger CPU set is invalid; affinity disabled");
        resolved.set_cpus(core::CpuSet{});
    }
    return resolved;
}

void stop_logger(LoggingState& current) noexcept {
    if (current.logger) {
        static_cast<void>(current.logger->flush());
        current.logger.reset();
    }
}

[[nodiscard]] core::Status start_logger(LoggingState& current, std::string_view base_dir) noexcept {
    const core::Status created = create_artifact_dir(base_dir);
    if (!created) {
        return created;
    }
    if (::access(std::string(base_dir).c_str(), W_OK | X_OK) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    auto logger = std::make_unique<ExecutionManagerFileLogger>();
    logging::FileLoggerConfig config{};
    config.app_name = current.execution_manager_name;
    config.base_dir = base_dir;
    config.file_name = "execution_manager.jsonl";
    config.initial_level = current.level;
    config.worker_thread = current.logger_thread;
    config.rotation.kind = logging::RotationSchedule::None;
    config.max_file_size_bytes =
        current.settings.global_loaded ? current.settings.options.max_file_size_bytes : 0;
    if (current.settings.global_loaded) {
        const auto per_second =
            current.settings.options.spam_protection.max_messages_per_sec_per_shard;
        if (per_second != 0) {
            config.throttle.enabled = true;
            config.throttle.max_records_per_window = per_second;
            config.throttle.window_ns = nanoseconds_per_second;
        }
    }

    const core::Status initialized = logger->initialize(config);
    if (!initialized) {
        return initialized;
    }
    for (int attempt = 0; attempt < 100 && !logger->writer_started(); ++attempt) {
        sleep_ms(10);
    }
    if (!logger->writer_started() || logger->writer_start_code() != core::ErrorCode::Ok) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    current.logger = std::move(logger);
    current.base_dir = std::string(base_dir);
    return {};
}

} // namespace

core::Status load_execution_manager_logging_settings(std::string_view global_settings_path,
                                          logging::Settings& out) noexcept {
    out = logging::Settings{};
    if (global_settings_path.empty()) {
        return {};
    }
    core::SettingsPaths paths{};
    paths.global_file = std::string(global_settings_path) + "/logging.json";
    return logging::load_logging_settings(paths, out);
}

core::Status init_execution_manager_logging(std::string_view primary_dir, const logging::Settings& settings,
                                 core::LogLevel level, const core::ThreadSpec& logger_thread,
                                 std::string_view execution_manager_name) noexcept {
    shutdown_execution_manager_logging();

    LoggingState& current = state();
    current.settings = settings;
    current.level = level;
    current.execution_manager_name =
        execution_manager_name.empty() ? std::string("nexigen-exec") : std::string(execution_manager_name);
    const core::Result<core::ThreadSpec> resolved = resolve_logger_thread(logger_thread, settings);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    current.logger_thread = *resolved;

    core::Status primary = start_logger(current, primary_dir);
    if (primary) {
        current.configured = true;
        return {};
    }

    warn_stderr("primary log path is unavailable; using " + fallback_directory());
    const std::string fallback = fallback_directory();
    const core::Status secondary = start_logger(current, fallback);
    if (secondary) {
        current.configured = true;
        return {};
    }

    warn_stderr("execution_manager file logging is unavailable; continuing with stderr");
    stop_logger(current);
    current.base_dir.clear();
    current.configured = true;
    return secondary;
}

void suspend_execution_manager_logging() noexcept {
    LoggingState& current = state();
    stop_logger(current);
}

core::Status resume_execution_manager_logging() noexcept {
    LoggingState& current = state();
    if (!current.configured || current.base_dir.empty()) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    return start_logger(current, current.base_dir);
}

core::Status flush_execution_manager_logging() noexcept {
    LoggingState& current = state();
    if (!current.logger) {
        return {};
    }
    return current.logger->flush();
}

void log_execution_manager(core::LogLevel level, std::string_view message) noexcept {
    LoggingState& current = state();
    if (!current.logger) {
        return;
    }
    current.logger->log(level, "{}", message);
}

void warn_execution_manager(std::string_view message) noexcept {
    LoggingState& current = state();
    if (current.logger) {
        current.logger->log(core::LogLevel::Warning, "{}", message);
    } else {
        warn_stderr(message);
    }
}

bool execution_manager_logging_active() noexcept {
    return state().logger != nullptr;
}

std::string execution_manager_logging_path() {
    const LoggingState& current = state();
    if (!current.logger) {
        return {};
    }
    return current.base_dir + "/execution_manager.jsonl";
}

std::uint64_t execution_manager_logging_writer_mask() noexcept {
    const LoggingState& current = state();
    return current.logger ? current.logger->writer_mask() : 0;
}

void shutdown_execution_manager_logging() noexcept {
    LoggingState& current = state();
    stop_logger(current);
    current.configured = false;
    current.base_dir.clear();
    current.execution_manager_name = "nexigen-exec";
    current.settings = logging::Settings{};
    current.logger_thread = core::ThreadSpec{};
}

core::Status apply_execution_manager_thread_spec(const core::ThreadSpec& spec, core::ThreadApplyReport& report,
                                      std::string_view context) noexcept {
    static_cast<void>(context);
    report = core::ThreadApplyReport{};
    if (!has_thread_request(spec)) {
        return {};
    }
    core::ThreadSpec effective = spec;
    if (!detail::cpus_within_online(effective.cpus())) {
        if (effective.apply_mode() == core::ThreadApplyMode::Required) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        warn_stderr("execution_manager control CPU set is invalid; affinity disabled");
        effective.set_cpus(core::CpuSet{});
    }
    const core::Status applied = core::apply_thread_spec_self(effective, report);
    if (!applied) {
        return applied;
    }
    if (report.first_error() != core::ErrorCode::Ok) {
        return std::unexpected(report.first_error());
    }
    return {};
}

core::Status apply_execution_manager_thread_spec(const core::ThreadSpec& spec,
                                      std::string_view context) noexcept {
    core::ThreadApplyReport report{};
    return apply_execution_manager_thread_spec(spec, report, context);
}

std::string describe_thread_apply_report(const core::ThreadApplyReport& report) {
    const auto joined = [](std::uint32_t mask) {
        std::string out;
        const auto add = [&out](std::string_view label) {
            if (!out.empty()) {
                out += ',';
            }
            out += label;
        };
        if ((mask & core::ThreadApplyReport::name_mask) != 0U) {
            add("name");
        }
        if ((mask & core::ThreadApplyReport::affinity_mask) != 0U) {
            add("affinity");
        }
        if ((mask & core::ThreadApplyReport::scheduler_mask) != 0U) {
            add("scheduler");
        }
        return out.empty() ? std::string("none") : out;
    };
    return std::format("requested={} applied={} failed={} rollback_failed={} "
                       "first_error={}",
                       joined(report.requested()), joined(report.applied()),
                       joined(report.failed()), joined(report.rollback_failed()),
                       core::to_string(report.first_error()));
}

core::Status apply_execution_manager_affinity(std::span<const std::uint32_t> cpus,
                                   std::string_view context) noexcept {
    if (cpus.empty()) {
        return {};
    }
    const core::Result<core::CpuSet> converted = core::CpuSet::from_cpus(cpus);
    if (!converted) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    core::ThreadSpec spec{};
    spec.set_cpus(*converted);
    return apply_execution_manager_thread_spec(spec, context);
}

core::Status reset_execution_manager_affinity() noexcept {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    const long online = detail::online_cpu_count();
    const long limit =
        online < static_cast<long>(CPU_SETSIZE) ? online : static_cast<long>(CPU_SETSIZE);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
    for (long cpu = 0; cpu < limit; ++cpu) {
        CPU_SET(cpu, &mask);
    }
#pragma GCC diagnostic pop
    if (::sched_setaffinity(0, sizeof(mask), &mask) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

} // namespace nexigen::exec
