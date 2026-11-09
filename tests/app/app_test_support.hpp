// Shared harness and fixtures for the nexigen::app test suite.
//
// The suites used to live in one 2.3k-line translation unit. They are now
// split per entity (stop reason, command line, settings, telemetry layout,
// infrastructure) so an edit recompiles one small TU, a failure points at the
// owning file, and diffs stay reviewable. Everything shared by more than one
// entity lives here.

#pragma once

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <signal.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <malloc.h>

#include <nlohmann/json.hpp>

#include "../support/test_temp.hpp"

#include <nexigen/app/command_line.hpp>
#include <nexigen/app/infrastructure.hpp>
#include <nexigen/app/app_settings.hpp>
#include <nexigen/app/infrastructure_stop_reason.hpp>
#include <nexigen/app/telemetry_layout.hpp>
#include <nexigen/chrono/json_file_chrono_sink.hpp>
#include <nexigen/chrono/null_chrono_sink.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/metrics/json_file_metric_sink.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>
#include <nexigen/tracing/json_file_trace_sink.hpp>
#include <nexigen/tracing/null_trace_sink.hpp>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define NX_SANITIZER_BUILD 1
#endif

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;
namespace logging = nexigen::logging;
namespace metrics = nexigen::metrics;
namespace tracing = nexigen::tracing;

inline int g_checks = 0;
inline int g_failures = 0;

#define NX_CHECK(condition)                                                                        \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(condition)) {                                                                        \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);              \
        }                                                                                          \
    } while (false)

inline void run_suite(const char* name, void (*body)()) {
    const int before = g_failures;
    body();
    std::printf("[%-20s] %s (%d failed)\n", name, g_failures == before ? "OK" : "FAILED",
                g_failures - before);
}

// --- capture logger (test-only: formats immediately into a global vector) ---
inline std::vector<std::string> g_log_lines;

class CaptureLogger {
public:
    void set_level(core::LogLevel level) noexcept { level_ = level; }
    [[nodiscard]] core::LogLevel level() const noexcept { return level_; }

    template <class... Args>
    void log(core::LogLevel, logging::detail::format_param_t<Args...> fmt,
             Args&&... args) noexcept {
        g_log_lines.push_back(std::vformat(fmt.fmt.get(), std::make_format_args(args...)));
    }

    template <class... Args>
    void verbose(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void debug(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void info(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void warning(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void error(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void fatal(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    [[nodiscard]] core::Status flush() noexcept { return {}; }

private:
    core::LogLevel level_{core::LogLevel::Info};
};

static_assert(logging::concepts::Logger<CaptureLogger>);

class FlushFailLogger : public CaptureLogger {
public:
    [[nodiscard]] core::Status flush() noexcept {
        return std::unexpected(core::ErrorCode::SystemError);
    }
};

static_assert(logging::concepts::Logger<FlushFailLogger>);

struct TestPolicy {
    using Logger = CaptureLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using ChronoSink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out, const logging::Settings&,
                                       std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&, std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<ChronoSink>& out,
                                            const chrono::Settings&) noexcept {
        out.emplace();
        return {};
    }
};

/// File-backed policy used by the telemetry-layout integration test. It reads
/// every sink from the settings value and never uses a hard-coded path.
struct ResolvedPathPolicy {
    using Logger = logging::FileLogger<>;
    using MetricsSink = metrics::JsonFileMetricSink;
    using TraceSink = tracing::JsonFileTraceSink;
    using ChronoSink = chrono::JsonFileChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Settings& settings,
                                       std::string_view app_name) noexcept {
        logging::FileLoggerConfig config{};
        const core::Status applied = logging::apply_to(settings, config);
        if (!applied) {
            return applied;
        }
        config.app_name = app_name;
        config.rotation.kind = logging::RotationSchedule::None;
        out.emplace();
        return out->initialize(config);
    }
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings& settings,
                                             std::string_view app_name) noexcept {
        if (settings.sink.path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        metrics::JsonFileMetricSinkConfig config = settings.sink;
        config.app_name = std::string(app_name);
        out.emplace(config);
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<TraceSink>& out,
                                           const tracing::Settings& settings,
                                           std::string_view app_name) noexcept {
        if (settings.sink.path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        tracing::JsonFileTraceSinkConfig config = settings.sink;
        config.app_name = std::string(app_name);
        out.emplace(config);
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<ChronoSink>& out,
                                            const chrono::Settings& settings) noexcept {
        if (settings.sink.path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        out.emplace(settings.sink);
        return {};
    }
};

struct RtPolicy {
    using Logger = logging::FileLogger<128, 128>;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using ChronoSink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Settings& settings,
                                       std::string_view app_name) noexcept {
        logging::FileLoggerConfig config{};
        const core::Status applied = logging::apply_to(settings, config);
        if (!applied) {
            return applied;
        }
        config.app_name = app_name;
        config.rotation.kind = logging::RotationSchedule::None;
        out.emplace();
        return out->initialize(config);
    }
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&, std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<ChronoSink>& out,
                                            const chrono::Settings&) noexcept {
        out.emplace();
        return {};
    }
};

struct FailLoggerPolicy {
    using Logger = CaptureLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using ChronoSink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>&, const logging::Settings&,
                                       std::string_view) noexcept {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&, std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<ChronoSink>& out,
                                            const chrono::Settings&) noexcept {
        out.emplace();
        return {};
    }
};

struct FailMetricsPolicy {
    using Logger = CaptureLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using ChronoSink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out, const logging::Settings&,
                                       std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>&, const metrics::Settings&,
                                             std::string_view) noexcept {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<ChronoSink>& out,
                                            const chrono::Settings&) noexcept {
        out.emplace();
        return {};
    }
};

struct FailTracePolicy {
    using Logger = CaptureLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using ChronoSink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out, const logging::Settings&,
                                       std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&, std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<TraceSink>&, const tracing::Settings&,
                                           std::string_view) noexcept {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    static core::Status emplace_chrono_sink(std::optional<ChronoSink>& out,
                                            const chrono::Settings&) noexcept {
        out.emplace();
        return {};
    }
};

struct FlushFailPolicy {
    using Logger = FlushFailLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using ChronoSink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out, const logging::Settings&,
                                       std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&, std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<ChronoSink>& out,
                                            const chrono::Settings&) noexcept {
        out.emplace();
        return {};
    }
};

struct FailChronoPolicy {
    using Logger = CaptureLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using ChronoSink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out, const logging::Settings&,
                                       std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&, std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           std::string_view) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<ChronoSink>&,
                                            const chrono::Settings&) noexcept {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
};

template <class Policy> using TestInfra = app::Infrastructure<Policy, 64, 4>;

template <class Policy> [[nodiscard]] std::unique_ptr<TestInfra<Policy>> make_test_infra() {
    auto created = app::make_infrastructure<Policy, 64, 4>();
    NX_CHECK(created.has_value());
    return created.has_value() ? std::move(*created) : nullptr;
}

template <class T, class = void> struct has_from_json : std::false_type {};
template <class T>
struct has_from_json<
    T, std::void_t<decltype(from_json(std::declval<const nlohmann::json&>(), std::declval<T&>()))>>
    : std::true_type {};

static_assert(!has_from_json<metrics::Settings>::value);
static_assert(!has_from_json<tracing::Settings>::value);
static_assert(!has_from_json<app::AppSettingsPaths>::value);
static_assert(!has_from_json<app::Settings>::value);
static_assert(has_from_json<app::CommandLineOptions>::value);
static_assert(has_from_json<app::CommandLineDiagnostic>::value);
static_assert(has_from_json<app::CommandLineError>::value);
static_assert(has_from_json<core::SettingsPaths>::value);

// Design D9: the sink member does not turn settings into a polymorphic or
// move-only type; they stay ordinary copyable/movable value types.
template <class T>
constexpr bool is_plain_value_type =
    std::is_copy_constructible_v<T> && std::is_move_constructible_v<T> &&
    std::is_copy_assignable_v<T> && std::is_move_assignable_v<T> &&
    std::is_nothrow_destructible_v<T> && !std::is_polymorphic_v<T>;

static_assert(is_plain_value_type<metrics::Settings>);
static_assert(is_plain_value_type<tracing::Settings>);
static_assert(is_plain_value_type<chrono::Settings>);

struct EnvGuard {
    std::string name;
    bool had{false};
    std::string saved;

    explicit EnvGuard(const char* variable) : name(variable) {
        const char* const value = std::getenv(variable);
        had = value != nullptr;
        if (had) {
            saved = value;
        }
    }
    ~EnvGuard() {
        if (had) {
            static_cast<void>(::setenv(name.c_str(), saved.c_str(), 1));
        } else {
            static_cast<void>(::unsetenv(name.c_str()));
        }
    }
    void set(const char* value) { static_cast<void>(::setenv(name.c_str(), value, 1)); }
    void clear() { static_cast<void>(::unsetenv(name.c_str())); }
};

inline std::string unique_path(std::string_view prefix) {
    return "/tmp/opencode/" + std::string(prefix) + "_" +
           std::to_string(static_cast<long long>(::getpid()));
}

inline void write_text(const std::string& path, std::string_view text) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    NX_CHECK(out.is_open());
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

inline void make_dir(const std::string& path) {
    NX_CHECK(::mkdir(path.c_str(), 0700) == 0);
}

inline app::CommandLineResult parse_args(std::initializer_list<std::string_view> args) {
    return app::parse_command_line(std::span<const std::string_view>{args.begin(), args.size()});
}

[[nodiscard]] inline bool has_diagnostic(const app::CommandLineError& error,
                                         std::string_view option) {
    for (const app::CommandLineDiagnostic& diagnostic : error.diagnostics) {
        if (diagnostic.option == option) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool message_contains(const app::CommandLineError& error,
                                           std::string_view text) {
    for (const app::CommandLineDiagnostic& diagnostic : error.diagnostics) {
        if (diagnostic.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}
