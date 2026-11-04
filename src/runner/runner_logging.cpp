// nexigen::runner — runner-owned logging and CPU affinity
// (change nexigen-run-logging; spec runner/logging).
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/logging_settings.hpp>
#include <nexigen/runner/detail/sys_helpers.hpp>
#include <nexigen/runner/instance_lifecycle.hpp>
#include <nexigen/runner/runner_logging.hpp>

namespace nexigen::runner {

using detail::nanoseconds_per_second;
using detail::sleep_ms;

namespace {

using RunnerFileLogger = logging::FileLogger<64, 256, 4>;

struct LoggingState {
    std::unique_ptr<RunnerFileLogger> logger{};
    std::string base_dir{};
    std::string runner_name{"nexigen-run"};
    logging::Settings settings{};
    logging::LogLevel level{logging::LogLevel::Info};
    core::ThreadSpec logger_thread{};
    bool configured{false};
};

LoggingState& state() {
    static LoggingState instance{};
    return instance;
}

void warn_stderr(std::string_view message) noexcept {
    std::fprintf(stderr, "nexigen-run: %.*s\n",
                 static_cast<int>(message.size()), message.data());
}

[[nodiscard]] long online_cpus() noexcept {
    const long count = ::sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? count : static_cast<long>(CPU_SETSIZE);
}

[[nodiscard]] std::string fallback_directory() {
    char buffer[4096]{};
    if (::getcwd(buffer, sizeof(buffer)) == nullptr) {
        return "/tmp/.nexigen-run";
    }
    return std::string(buffer) + "/.nexigen-run";
}

[[nodiscard]] bool has_thread_request(const core::ThreadSpec& spec) noexcept {
    return !spec.cpus().empty() || !spec.name().empty() ||
           spec.scheduler_configured() ||
           spec.apply_mode() == core::ThreadApplyMode::Required;
}

[[nodiscard]] bool online_cpu_set(const core::CpuSet& cpus) noexcept {
    const auto online = static_cast<std::uint32_t>(online_cpus());
    for (std::uint32_t cpu = 0; cpu <= core::CpuSet::max_cpu_id; ++cpu) {
        if (cpus.contains(cpu) &&
            (cpu >= online || cpu >= static_cast<std::uint32_t>(CPU_SETSIZE))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] core::Result<core::ThreadSpec> resolve_logger_thread(
    const core::ThreadSpec& requested,
    const logging::Settings& settings) noexcept {
    core::ThreadSpec resolved = has_thread_request(requested)
                                    ? requested
                                    : (settings.global_loaded
                                           ? settings.options.worker_thread
                                           : core::ThreadSpec{});
    const core::Status valid = resolved.validate();
    if (!valid) {
        if (resolved.apply_mode() == core::ThreadApplyMode::Required) {
            return std::unexpected(valid.error());
        }
        warn_stderr("logger thread spec is invalid; worker attributes disabled");
        return core::ThreadSpec{};
    }
    if (!online_cpu_set(resolved.cpus())) {
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

[[nodiscard]] core::Status start_logger(
    LoggingState& current, std::string_view base_dir) noexcept {
    const core::Status created = create_artifact_dir(base_dir);
    if (!created) {
        return created;
    }
    if (::access(std::string(base_dir).c_str(), W_OK | X_OK) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    auto logger = std::make_unique<RunnerFileLogger>();
    logging::FileLoggerConfig config{};
    config.app_name = current.runner_name;
    config.base_dir = base_dir;
    config.file_name = "runner.jsonl";
    config.initial_level = current.level;
    config.worker_thread = current.logger_thread;
    config.rotation.kind = logging::RotationSchedule::None;
    config.max_file_size_bytes =
        current.settings.global_loaded
            ? current.settings.options.max_file_size_bytes
            : 0;
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
    if (!logger->writer_started() ||
        logger->writer_start_code() != core::ErrorCode::Ok) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    current.logger = std::move(logger);
    current.base_dir = std::string(base_dir);
    return {};
}

} // namespace

core::Status load_runner_logging_settings(
    std::string_view global_settings_path,
    logging::Settings& out) noexcept {
    out = logging::Settings{};
    if (global_settings_path.empty()) {
        return {};
    }
    core::SettingsPaths paths{};
    paths.global_file =
        std::string(global_settings_path) + "/logging.json";
    return logging::load_logging_settings(paths, out);
}

core::Status init_runner_logging(
    std::string_view primary_dir,
    const logging::Settings& settings,
    logging::LogLevel level,
    const core::ThreadSpec& logger_thread,
    std::string_view runner_name) noexcept {
    shutdown_runner_logging();

    LoggingState& current = state();
    current.settings = settings;
    current.level = level;
    current.runner_name =
        runner_name.empty() ? std::string("nexigen-run") : std::string(runner_name);
    const core::Result<core::ThreadSpec> resolved =
        resolve_logger_thread(logger_thread, settings);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    current.logger_thread = *resolved;

    core::Status primary = start_logger(current, primary_dir);
    if (primary) {
        current.configured = true;
        return {};
    }

    warn_stderr("primary log path is unavailable; using " +
                fallback_directory());
    const std::string fallback = fallback_directory();
    const core::Status secondary = start_logger(current, fallback);
    if (secondary) {
        current.configured = true;
        return {};
    }

    warn_stderr("runner file logging is unavailable; continuing with stderr");
    stop_logger(current);
    current.base_dir.clear();
    current.configured = true;
    return secondary;
}

void suspend_runner_logging() noexcept {
    LoggingState& current = state();
    stop_logger(current);
}

core::Status resume_runner_logging() noexcept {
    LoggingState& current = state();
    if (!current.configured || current.base_dir.empty()) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    return start_logger(current, current.base_dir);
}

core::Status flush_runner_logging() noexcept {
    LoggingState& current = state();
    if (!current.logger) {
        return {};
    }
    return current.logger->flush();
}

void log_runner(logging::LogLevel level, std::string_view message) noexcept {
    LoggingState& current = state();
    if (!current.logger) {
        return;
    }
    current.logger->log(level, "{}", message);
}

void warn_runner(std::string_view message) noexcept {
    LoggingState& current = state();
    if (current.logger) {
        current.logger->log(logging::LogLevel::Warning, "{}", message);
    } else {
        warn_stderr(message);
    }
}

bool runner_logging_active() noexcept {
    return state().logger != nullptr;
}

std::string runner_logging_path() {
    const LoggingState& current = state();
    if (!current.logger) {
        return {};
    }
    return current.base_dir + "/runner.jsonl";
}

std::uint64_t runner_logging_writer_mask() noexcept {
    const LoggingState& current = state();
    return current.logger ? current.logger->writer_mask() : 0;
}

void shutdown_runner_logging() noexcept {
    LoggingState& current = state();
    stop_logger(current);
    current.configured = false;
    current.base_dir.clear();
    current.runner_name = "nexigen-run";
    current.settings = logging::Settings{};
    current.logger_thread = core::ThreadSpec{};
}

core::Status apply_runner_thread_spec(
    const core::ThreadSpec& spec, std::string_view context) noexcept {
    static_cast<void>(context);
    if (!has_thread_request(spec)) {
        return {};
    }
    const core::Status valid = spec.validate();
    if (!valid) {
        return valid;
    }
    core::ThreadSpec effective = spec;
    if (!online_cpu_set(effective.cpus())) {
        if (effective.apply_mode() == core::ThreadApplyMode::Required) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        warn_stderr("runner control CPU set is invalid; affinity disabled");
        effective.set_cpus(core::CpuSet{});
    }
    core::ThreadApplyReport report{};
    const core::Status applied = core::apply_thread_spec_self(effective, report);
    if (!applied) {
        return applied;
    }
    if (report.first_error() != core::ErrorCode::Ok) {
        return std::unexpected(report.first_error());
    }
    return {};
}

core::Status apply_runner_affinity(
    std::span<const std::uint32_t> cpus, std::string_view context) noexcept {
    if (cpus.empty()) {
        return {};
    }
    const core::Result<core::CpuSet> converted = core::CpuSet::from_cpus(cpus);
    if (!converted) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    core::ThreadSpec spec{};
    spec.set_cpus(*converted);
    return apply_runner_thread_spec(spec, context);
}

core::Status reset_runner_affinity() noexcept {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    const long online = online_cpus();
    const long limit = online < static_cast<long>(CPU_SETSIZE)
                           ? online
                           : static_cast<long>(CPU_SETSIZE);
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

} // namespace nexigen::runner
