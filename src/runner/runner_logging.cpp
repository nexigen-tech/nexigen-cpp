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
    std::vector<std::uint32_t> logger_cpus{};
    std::uint64_t logger_mask{0};
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

[[nodiscard]] bool valid_cpu_list(
    std::span<const std::uint32_t> cpus, std::uint32_t limit) noexcept {
    const auto online = static_cast<std::uint32_t>(online_cpus());
    const std::uint32_t effective_limit = limit < online ? limit : online;
    for (std::size_t index = 0; index < cpus.size(); ++index) {
        if (cpus[index] >= effective_limit) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (cpus[previous] == cpus[index]) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::uint64_t mask_from_cpus(
    std::span<const std::uint32_t> cpus) noexcept {
    std::uint64_t mask = 0;
    for (const std::uint32_t cpu : cpus) {
        if (cpu < 64U) {
            mask |= (1ULL << cpu);
        }
    }
    return mask;
}

[[nodiscard]] std::uint64_t resolve_logger_mask(
    std::span<const std::uint32_t> logger_cpus,
    const logging::Settings& settings) noexcept {
    if (!logger_cpus.empty()) {
        if (!valid_cpu_list(logger_cpus, 64U)) {
            warn_stderr("runner_logger_cpus is invalid; logger affinity disabled");
            return 0;
        }
        return mask_from_cpus(logger_cpus);
    }
    if (!settings.global_loaded ||
        settings.options.flush_cpu_affinity.empty()) {
        return 0;
    }
    std::vector<std::uint32_t> cpus;
    cpus.reserve(settings.options.flush_cpu_affinity.size());
    for (const std::uint64_t core : settings.options.flush_cpu_affinity) {
        if (core > UINT32_MAX) {
            warn_stderr("flush_cpu_affinity is invalid; logger affinity disabled");
            return 0;
        }
        cpus.push_back(static_cast<std::uint32_t>(core));
    }
    if (!valid_cpu_list(cpus, 64U)) {
        warn_stderr("flush_cpu_affinity is invalid; logger affinity disabled");
        return 0;
    }
    return mask_from_cpus(cpus);
}

void stop_logger(LoggingState& current) noexcept {
    if (current.logger) {
        static_cast<void>(current.logger->flush());
        current.logger.reset();
    }
    current.logger_mask = 0;
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
    config.core_mask = current.logger_mask;
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
    std::span<const std::uint32_t> logger_cpus,
    std::string_view runner_name) noexcept {
    shutdown_runner_logging();

    LoggingState& current = state();
    current.settings = settings;
    current.level = level;
    current.runner_name =
        runner_name.empty() ? std::string("nexigen-run") : std::string(runner_name);
    current.logger_cpus.assign(logger_cpus.begin(), logger_cpus.end());
    current.logger_mask = resolve_logger_mask(logger_cpus, settings);

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
    current.logger_cpus.clear();
}

core::Status apply_runner_affinity(
    std::span<const std::uint32_t> cpus, std::string_view context) noexcept {
    static_cast<void>(context);
    if (cpus.empty()) {
        return {};
    }
    const auto online = static_cast<std::uint32_t>(online_cpus());
    cpu_set_t mask;
    CPU_ZERO(&mask);
    for (std::size_t index = 0; index < cpus.size(); ++index) {
        if (cpus[index] >= online ||
            cpus[index] >= static_cast<std::uint32_t>(CPU_SETSIZE)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (cpus[previous] == cpus[index]) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
        CPU_SET(cpus[index], &mask);
#pragma GCC diagnostic pop
    }
    if (::sched_setaffinity(0, sizeof(mask), &mask) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
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
