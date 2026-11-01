// nexigen::app tests — zero-dependency harness (same NX_CHECK/run_suite
// style as the other modules), change add-app-class.
//
// Covers the stop-reason contract, per-subsystem settings loaders, the Infrastructure
// lifecycle (initialize/run/shutdown/current), the metric and tracer factory
// accessors, rollback on a failing hook, and real SIGINT/SIGTERM delivery.
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

namespace {

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;
namespace logging = nexigen::logging;
namespace metrics = nexigen::metrics;
namespace tracing = nexigen::tracing;

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(condition)                                                            \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(condition)) {                                                            \
            ++g_failures;                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
        }                                                                              \
    } while (false)

void run_suite(const char* name, void (*body)()) {
    const int before = g_failures;
    body();
    std::printf("[%-20s] %s (%d failed)\n", name,
                g_failures == before ? "OK" : "FAILED", g_failures - before);
}

// --- capture logger (test-only: formats immediately into a global vector) ---
std::vector<std::string> g_log_lines;

class Capture_logger {
public:
    void set_level(logging::LogLevel level) noexcept { level_ = level; }
    [[nodiscard]] logging::LogLevel level() const noexcept { return level_; }

    template <class... Args>
    void log(logging::LogLevel, logging::detail::format_param_t<Args...> fmt,
             Args&&... args) noexcept {
        g_log_lines.push_back(
            std::vformat(fmt.fmt.get(), std::make_format_args(args...)));
    }

    template <class... Args>
    void verbose(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(logging::LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void debug(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(logging::LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void info(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(logging::LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void warning(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(logging::LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void error(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(logging::LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void fatal(logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(logging::LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    [[nodiscard]] core::Status flush() noexcept { return {}; }

private:
    logging::LogLevel level_{logging::LogLevel::Info};
};

static_assert(logging::concepts::Logger<Capture_logger>);

class Flush_fail_logger : public Capture_logger {
public:
    [[nodiscard]] core::Status flush() noexcept {
        return std::unexpected(core::ErrorCode::SystemError);
    }
};

static_assert(logging::concepts::Logger<Flush_fail_logger>);

struct Test_policy {
    using Logger = Capture_logger;
    using Metrics_sink = metrics::NullMetricSink;
    using Trace_sink = tracing::NullTraceSink;
    using Chrono_sink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Logging_settings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_metrics_sink(std::optional<Metrics_sink>& out,
                                             const app::MetricsSettings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<Trace_sink>& out,
                                           const app::TracingSettings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<Chrono_sink>& out,
                                            const chrono::Chrono_settings&) noexcept {
        out.emplace();
        return {};
    }
};

/// File-backed policy used by the telemetry-layout integration test. It reads
/// every sink from the settings value and never uses a hard-coded path.
struct Resolved_path_policy {
    using Logger = logging::FileLogger<>;
    using Metrics_sink = metrics::JsonFileMetricSink;
    using Trace_sink = tracing::JsonFileTraceSink;
    using Chrono_sink = chrono::JsonFileChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Logging_settings& settings) noexcept {
        logging::FileLogger_config config{};
        const core::Status applied = logging::apply_to(settings, config);
        if (!applied) {
            return applied;
        }
        config.rotation.kind = logging::Rotation_schedule::None;
        out.emplace();
        return out->initialize(config);
    }
    static core::Status emplace_metrics_sink(std::optional<Metrics_sink>& out,
                                             const app::MetricsSettings& settings) noexcept {
        if (settings.sink.path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        out.emplace(settings.sink);
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<Trace_sink>& out,
                                           const app::TracingSettings& settings) noexcept {
        if (settings.sink.path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        out.emplace(settings.sink);
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<Chrono_sink>& out,
                                            const chrono::Chrono_settings& settings) noexcept {
        if (settings.sink.path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        out.emplace(settings.sink);
        return {};
    }
};

struct Fail_logger_policy {
    using Logger = Capture_logger;
    using Metrics_sink = metrics::NullMetricSink;
    using Trace_sink = tracing::NullTraceSink;
    using Chrono_sink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>&,
                                       const logging::Logging_settings&) noexcept {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    static core::Status emplace_metrics_sink(std::optional<Metrics_sink>& out,
                                             const app::MetricsSettings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<Trace_sink>& out,
                                           const app::TracingSettings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<Chrono_sink>& out,
                                            const chrono::Chrono_settings&) noexcept {
        out.emplace();
        return {};
    }
};

struct Fail_metrics_policy {
    using Logger = Capture_logger;
    using Metrics_sink = metrics::NullMetricSink;
    using Trace_sink = tracing::NullTraceSink;
    using Chrono_sink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Logging_settings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_metrics_sink(std::optional<Metrics_sink>&,
                                             const app::MetricsSettings&) noexcept {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    static core::Status emplace_trace_sink(std::optional<Trace_sink>& out,
                                           const app::TracingSettings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<Chrono_sink>& out,
                                            const chrono::Chrono_settings&) noexcept {
        out.emplace();
        return {};
    }
};

struct Fail_trace_policy {
    using Logger = Capture_logger;
    using Metrics_sink = metrics::NullMetricSink;
    using Trace_sink = tracing::NullTraceSink;
    using Chrono_sink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Logging_settings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_metrics_sink(std::optional<Metrics_sink>& out,
                                             const app::MetricsSettings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<Trace_sink>&,
                                           const app::TracingSettings&) noexcept {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    static core::Status emplace_chrono_sink(std::optional<Chrono_sink>& out,
                                            const chrono::Chrono_settings&) noexcept {
        out.emplace();
        return {};
    }
};

struct Flush_fail_policy {
    using Logger = Flush_fail_logger;
    using Metrics_sink = metrics::NullMetricSink;
    using Trace_sink = tracing::NullTraceSink;
    using Chrono_sink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Logging_settings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_metrics_sink(std::optional<Metrics_sink>& out,
                                             const app::MetricsSettings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<Trace_sink>& out,
                                           const app::TracingSettings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<Chrono_sink>& out,
                                            const chrono::Chrono_settings&) noexcept {
        out.emplace();
        return {};
    }
};

struct Fail_chrono_policy {
    using Logger = Capture_logger;
    using Metrics_sink = metrics::NullMetricSink;
    using Trace_sink = tracing::NullTraceSink;
    using Chrono_sink = chrono::NullChronoSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Logging_settings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_metrics_sink(std::optional<Metrics_sink>& out,
                                             const app::MetricsSettings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<Trace_sink>& out,
                                           const app::TracingSettings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_chrono_sink(std::optional<Chrono_sink>&,
                                            const chrono::Chrono_settings&) noexcept {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
};

template <class Policy>
using Test_infra = app::Infrastructure<Policy, 64, 4>;

template <class Policy>
[[nodiscard]] std::unique_ptr<Test_infra<Policy>> make_test_infra() {
    auto created = app::make_infrastructure<Policy, 64, 4>();
    NX_CHECK(created.has_value());
    return created.has_value() ? std::move(*created) : nullptr;
}

template <class T, class = void>
struct has_from_json : std::false_type {};
template <class T>
struct has_from_json<
    T, std::void_t<decltype(from_json(std::declval<const nlohmann::json&>(),
                                      std::declval<T&>()))>> : std::true_type {};

static_assert(!has_from_json<app::MetricsSettings>::value);
static_assert(!has_from_json<app::TracingSettings>::value);
static_assert(!has_from_json<app::AppSettingsPaths>::value);
static_assert(!has_from_json<app::AppSettings>::value);
static_assert(has_from_json<app::CommandLineOptions>::value);
static_assert(has_from_json<app::CommandLineDiagnostic>::value);
static_assert(has_from_json<app::CommandLineError>::value);
static_assert(has_from_json<app::Settings_paths>::value);

// Design D9: the sink member does not turn settings into a polymorphic or
// move-only type; they stay ordinary copyable/movable value types.
template <class T>
constexpr bool is_plain_value_type =
    std::is_copy_constructible_v<T> && std::is_move_constructible_v<T> &&
    std::is_copy_assignable_v<T> && std::is_move_assignable_v<T> &&
    std::is_nothrow_destructible_v<T> && !std::is_polymorphic_v<T>;

static_assert(is_plain_value_type<app::MetricsSettings>);
static_assert(is_plain_value_type<app::TracingSettings>);
static_assert(is_plain_value_type<chrono::Chrono_settings>);

struct Env_guard {
    std::string name;
    bool had{false};
    std::string saved;

    explicit Env_guard(const char* variable) : name(variable) {
        const char* const value = std::getenv(variable);
        had = value != nullptr;
        if (had) {
            saved = value;
        }
    }
    ~Env_guard() {
        if (had) {
            static_cast<void>(::setenv(name.c_str(), saved.c_str(), 1));
        } else {
            static_cast<void>(::unsetenv(name.c_str()));
        }
    }
    void set(const char* value) { static_cast<void>(::setenv(name.c_str(), value, 1)); }
    void clear() { static_cast<void>(::unsetenv(name.c_str())); }
};

std::string unique_path(std::string_view prefix) {
    return "/tmp/opencode/" + std::string(prefix) + "_" +
           std::to_string(static_cast<long long>(::getpid()));
}

void write_text(const std::string& path, std::string_view text) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    NX_CHECK(out.is_open());
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void make_dir(const std::string& path) {
    NX_CHECK(::mkdir(path.c_str(), 0700) == 0);
}

app::CommandLineResult parse_args(std::initializer_list<std::string_view> args) {
    return app::parse_command_line(
        std::span<const std::string_view>{args.begin(), args.size()});
}

[[nodiscard]] bool has_diagnostic(const app::CommandLineError& error,
                                  std::string_view option) {
    for (const app::CommandLineDiagnostic& diagnostic : error.diagnostics) {
        if (diagnostic.option == option) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool message_contains(const app::CommandLineError& error,
                                    std::string_view text) {
    for (const app::CommandLineDiagnostic& diagnostic : error.diagnostics) {
        if (diagnostic.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

void test_stop_reason() {
    NX_CHECK(app::to_string(app::InfrastructureStopReason::None) == "none");
    NX_CHECK(app::to_string(app::InfrastructureStopReason::Requested) == "requested");
    NX_CHECK(app::to_string(app::InfrastructureStopReason::Sigint) == "sigint");
    NX_CHECK(app::to_string(app::InfrastructureStopReason::Sigterm) == "sigterm");

    app::InfrastructureStopReason reason = app::InfrastructureStopReason::None;
    NX_CHECK(app::from_string("sigterm", reason));
    NX_CHECK(reason == app::InfrastructureStopReason::Sigterm);
    NX_CHECK(!app::from_string("bogus", reason));
    NX_CHECK(reason == app::InfrastructureStopReason::Sigterm);

    nlohmann::json json;
    app::to_json(json, app::InfrastructureStopReason::Sigint);
    NX_CHECK(json == "sigint");
    app::from_json(nlohmann::json("requested"), reason);
    NX_CHECK(reason == app::InfrastructureStopReason::Requested);
    app::from_json(nlohmann::json(7), reason);
    NX_CHECK(reason == app::InfrastructureStopReason::Requested);
    NX_CHECK(std::format("{}", app::InfrastructureStopReason::Sigterm) == "sigterm");
}

void test_settings_loaders() {
    Env_guard interval_env{"NEXIGEN_METRICS_COLLECT_INTERVAL_NS"};
    Env_guard metrics_mask_env{"NEXIGEN_METRICS_CORE_MASK"};
    Env_guard trace_mask_env{"NEXIGEN_TRACING_CORE_MASK"};
    Env_guard tracers_env{"NEXIGEN_TRACING_MAX_TRACERS"};
    Env_guard spans_env{"NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW"};
    Env_guard window_env{"NEXIGEN_TRACING_WINDOW_NS"};
    interval_env.clear();
    metrics_mask_env.clear();
    trace_mask_env.clear();
    tracers_env.clear();
    spans_env.clear();
    window_env.clear();

    const std::string base = unique_path("app_settings");
    const std::string metrics_global = base + "_metrics_global.json";
    const std::string metrics_local = base + "_metrics_local.json";
    const std::string tracing_global = base + "_tracing_global.json";
    const std::string tracing_local = base + "_tracing_local.json";

    write_text(metrics_global, R"({"core_mask":1,"max_counters":32})");
    write_text(metrics_local,
               R"({"collect_interval_ns":50000000,"max_counters":128})");
    write_text(tracing_global,
               R"({"core_mask":2,"max_tracers":8,)"
               R"("max_spans_per_window":100,"window_ns":1000})");
    write_text(tracing_local, R"({"max_tracers":16})");

    app::MetricsSettings metrics{};
    NX_CHECK(app::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
    NX_CHECK(metrics.global_loaded && metrics.local_loaded);
    NX_CHECK(metrics.options.core_mask == 1ULL);
    NX_CHECK(metrics.options.max_counters == 128U);
    NX_CHECK(metrics.options.collect_interval == std::chrono::nanoseconds{50'000'000});

    metrics_mask_env.set("4");
    NX_CHECK(app::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
    NX_CHECK(metrics.options.core_mask == 4ULL);
    metrics_mask_env.clear();

    write_text(metrics_local, "{");
    NX_CHECK(!app::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());
    NX_CHECK(metrics.options.core_mask == 1ULL); // global survived
    NX_CHECK(!metrics.diagnostics.empty());

    write_text(metrics_local, R"({"max_counters":"many"})");
    NX_CHECK(!app::load_metrics_settings({metrics_global, metrics_local}, metrics).has_value());

    app::TracingSettings tracing{};
    NX_CHECK(app::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
    NX_CHECK(tracing.options.core_mask == 2ULL);
    NX_CHECK(tracing.options.max_tracers == 16U);
    NX_CHECK(tracing.options.max_spans_per_window == 100ULL);
    NX_CHECK(tracing.options.window_ns == 1000ULL);

    tracers_env.set("32");
    NX_CHECK(app::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
    NX_CHECK(tracing.options.max_tracers == 32U);
    tracers_env.clear();

    write_text(tracing_local, R"({"max_tracers":100})");
    NX_CHECK(!app::load_tracing_settings({tracing_global, tracing_local}, tracing).has_value());
    NX_CHECK(tracing.options.max_tracers == 8U); // global survived

    // Unreadable supplied path (a directory) -> SystemError.
    NX_CHECK(!app::load_tracing_settings({"/tmp/opencode", ""}, tracing).has_value());
    NX_CHECK(tracing.diagnostics.back().source == logging::Settings_source::Global);

    // Aggregation: clean logging (empty paths) + one unreadable metrics pair.
    write_text(metrics_local, R"({"core_mask":1})");
    app::AppSettings app_settings{};
    app::AppSettingsPaths paths{};
    paths.metrics.global_file = "/tmp/opencode";
    NX_CHECK(!app::load_app_settings(paths, app_settings).has_value());
    NX_CHECK(app_settings.tracing.options.max_tracers == 16U); // defaults still populated

    paths.metrics.global_file = metrics_global;
    paths.metrics.local_file = "";
    write_text(tracing_local, R"({"max_tracers":16})");
    paths.tracing = {tracing_global, tracing_local};
    NX_CHECK(app::load_app_settings(paths, app_settings).has_value());

    const std::string text = app_settings.metrics.to_string();
    NX_CHECK(text.find("core_mask=0x1") != std::string::npos);
    NX_CHECK(std::format("{}", app_settings.metrics) == text);
    nlohmann::json json;
    app::to_json(json, app_settings.metrics);
    NX_CHECK(json["core_mask"] == 1U);
    NX_CHECK(app_settings.tracing.to_string().find("max_tracers") != std::string::npos);

    // Malformed JSON and non-object documents on both subsystems.
    write_text(metrics_global, "{");
    {
        app::MetricsSettings metrics_invalid{};
        NX_CHECK(!app::load_metrics_settings({metrics_global, ""}, metrics_invalid).has_value());
    }
    write_text(tracing_global, "[]");
    {
        app::TracingSettings tracing_invalid{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, ""}, tracing_invalid).has_value());
        NX_CHECK(!tracing_invalid.diagnostics.empty());
    }
    write_text(tracing_global, "{");
    {
        app::TracingSettings tracing_invalid{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, ""}, tracing_invalid).has_value());
    }

    // Serialization with non-empty diagnostics (loop branches).
    {
        app::MetricsSettings metrics_serialized{};
        metrics_serialized.global_loaded = true;
        metrics_serialized.diagnostics.push_back(
            {logging::Settings_source::Local, "core_mask", "bad"});
        NX_CHECK(metrics_serialized.to_string().find("bad") != std::string::npos);
        nlohmann::json serialized_json;
        app::to_json(serialized_json, metrics_serialized);
        NX_CHECK(serialized_json["diagnostics"].size() == 1U);
        app::TracingSettings tracing_serialized{};
        tracing_serialized.local_loaded = true;
        tracing_serialized.diagnostics.push_back(
            {logging::Settings_source::Global, "max_tracers", "bad"});
        NX_CHECK(tracing_serialized.to_string().find("bad") != std::string::npos);
        app::to_json(serialized_json, tracing_serialized);
        NX_CHECK(serialized_json["diagnostics"].size() == 1U);
    }

    // Malformed local documents on both subsystems.
    {
        write_text(metrics_global, R"({"core_mask":5})");
        write_text(metrics_local, "{");
        app::MetricsSettings metrics_local_bad{};
        NX_CHECK(!app::load_metrics_settings({metrics_global, metrics_local},
                                             metrics_local_bad)
                      .has_value());
        write_text(tracing_global, R"({"max_tracers":2})");
        write_text(tracing_local, "{");
        app::TracingSettings tracing_local_bad{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, tracing_local},
                                             tracing_local_bad)
                      .has_value());
    }

    // Aggregate InvalidArgument when subsystems load but contain diagnostics.
    {
        write_text(metrics_local, R"({"core_mask":"x"})");
        write_text(tracing_local, R"({"max_tracers":0})");
        app::AppSettingsPaths aggregate_paths{};
        aggregate_paths.metrics = {metrics_global, metrics_local};
        aggregate_paths.tracing = {tracing_global, tracing_local};
        app::AppSettings out{};
        const core::Status status = app::load_app_settings(aggregate_paths, out);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
    }

    static_cast<void>(std::remove(metrics_global.c_str()));
    static_cast<void>(std::remove(metrics_local.c_str()));
    static_cast<void>(std::remove(tracing_global.c_str()));
    static_cast<void>(std::remove(tracing_local.c_str()));
}

void test_settings_sink() {
    Env_guard interval_env{"NEXIGEN_METRICS_COLLECT_INTERVAL_NS"};
    Env_guard metrics_mask_env{"NEXIGEN_METRICS_CORE_MASK"};
    Env_guard trace_mask_env{"NEXIGEN_TRACING_CORE_MASK"};
    Env_guard tracers_env{"NEXIGEN_TRACING_MAX_TRACERS"};
    Env_guard spans_env{"NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW"};
    Env_guard window_env{"NEXIGEN_TRACING_WINDOW_NS"};
    interval_env.clear();
    metrics_mask_env.clear();
    trace_mask_env.clear();
    tracers_env.clear();
    spans_env.clear();
    window_env.clear();

    const std::string base = unique_path("app_settings_sink");
    const std::string metrics_global = base + "_mg.json";
    const std::string metrics_local = base + "_ml.json";
    const std::string tracing_global = base + "_tg.json";
    const std::string tracing_local = base + "_tl.json";

    // Local overrides global; absent local fields inherit global values.
    write_text(metrics_global,
               R"({"sink":{"path":"/tmp/global-metrics.jsonl","append":true}})");
    write_text(metrics_local,
               R"({"sink":{"path":"/tmp/local-metrics.jsonl","append":false}})");
    write_text(tracing_global,
               R"({"sink":{"path":"/tmp/global-traces.jsonl","rotation":"Hourly",)"
               R"("max_spans_per_window":10,"window_ns":100}})");
    write_text(tracing_local, R"({"sink":{"path":"/tmp/local-traces.jsonl"}})");
    {
        app::MetricsSettings metrics{};
        app::TracingSettings tracing{};
        NX_CHECK(app::load_metrics_settings({metrics_global, metrics_local}, metrics)
                     .has_value());
        NX_CHECK(metrics.sink.path == "/tmp/local-metrics.jsonl");
        NX_CHECK(!metrics.sink.append);
        NX_CHECK(app::load_tracing_settings({tracing_global, tracing_local}, tracing)
                     .has_value());
        NX_CHECK(tracing.sink.path == "/tmp/local-traces.jsonl");
        NX_CHECK(tracing.sink.rotation == tracing::RotationPolicy::Hourly);
        NX_CHECK(tracing.sink.max_spans_per_window == 10ULL);
        NX_CHECK(tracing.sink.window_ns == 100ULL);

        // Provider environment overrides never touch the sink.
        interval_env.set("50000000");
        trace_mask_env.set("3");
        NX_CHECK(app::load_metrics_settings({metrics_global, metrics_local}, metrics)
                     .has_value());
        NX_CHECK(app::load_tracing_settings({tracing_global, tracing_local}, tracing)
                     .has_value());
        NX_CHECK(metrics.sink.path == "/tmp/local-metrics.jsonl");
        NX_CHECK(tracing.sink.path == "/tmp/local-traces.jsonl");
        NX_CHECK(tracing.sink.rotation == tracing::RotationPolicy::Hourly);
        interval_env.clear();
        trace_mask_env.clear();
    }

    // Serialization carries both sink objects and the formatter matches.
    {
        app::MetricsSettings metrics{};
        app::TracingSettings tracing{};
        NX_CHECK(app::load_metrics_settings({metrics_global, metrics_local}, metrics)
                     .has_value());
        NX_CHECK(app::load_tracing_settings({tracing_global, tracing_local}, tracing)
                     .has_value());
        NX_CHECK(metrics.to_string().find("sink=JsonFileMetricSink_config") !=
                 std::string::npos);
        NX_CHECK(tracing.to_string().find("sink=JsonFileTraceSink_config") !=
                 std::string::npos);
        NX_CHECK(std::format("{}", metrics) == metrics.to_string());
        NX_CHECK(std::format("{}", tracing) == tracing.to_string());

        nlohmann::json json;
        app::to_json(json, metrics);
        NX_CHECK(json["sink"]["path"] == "/tmp/local-metrics.jsonl");
        NX_CHECK(json["sink"]["append"] == false);
        app::to_json(json, tracing);
        NX_CHECK(json["sink"]["path"] == "/tmp/local-traces.jsonl");
        NX_CHECK(json["sink"]["rotation"] == "Hourly");
        NX_CHECK(json["sink"]["max_spans_per_window"] == 10U);
        NX_CHECK(json["sink"]["window_ns"] == 100U);

        // AppSettings carries both sink objects through its members.
        app::AppSettings aggregate{};
        aggregate.metrics = metrics;
        aggregate.tracing = tracing;
        app::to_json(json, aggregate);
        NX_CHECK(json["metrics"]["sink"]["path"] == "/tmp/local-metrics.jsonl");
        NX_CHECK(json["tracing"]["sink"]["path"] == "/tmp/local-traces.jsonl");
        NX_CHECK(json["tracing"]["sink"]["rotation"] == "Hourly");
    }

    // Absent sink blocks leave the sink unconfigured.
    write_text(metrics_global, "{}");
    write_text(metrics_local, "{}");
    write_text(tracing_global, "{}");
    write_text(tracing_local, "{}");
    {
        app::MetricsSettings metrics{};
        app::TracingSettings tracing{};
        NX_CHECK(app::load_metrics_settings({metrics_global, metrics_local}, metrics)
                     .has_value());
        NX_CHECK(app::load_tracing_settings({tracing_global, tracing_local}, tracing)
                     .has_value());
        NX_CHECK(metrics.sink.path.empty());
        NX_CHECK(metrics.sink.append);
        NX_CHECK(tracing.sink.path.empty());
        NX_CHECK(tracing.sink.rotation == tracing::RotationPolicy::None);
        NX_CHECK(tracing.sink.max_spans_per_window == 0ULL);
        NX_CHECK(tracing.sink.window_ns == 0ULL);
    }

    // An empty path is accepted as "unconfigured", not a diagnostic.
    write_text(metrics_global, R"({"sink":{"path":""}})");
    {
        app::MetricsSettings metrics{};
        NX_CHECK(app::load_metrics_settings({metrics_global, ""}, metrics).has_value());
        NX_CHECK(metrics.sink.path.empty());
        NX_CHECK(metrics.diagnostics.empty());
    }

    // Invalid rotation keeps the previous policy and reports sink.rotation.
    write_text(tracing_global,
               R"({"sink":{"path":"/tmp/g.jsonl","rotation":"Hourly"}})");
    write_text(tracing_local,
               R"({"sink":{"path":"/tmp/l.jsonl","rotation":"Sometimes"}})");
    {
        app::TracingSettings tracing{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, tracing_local}, tracing)
                      .has_value());
        NX_CHECK(tracing.sink.path == "/tmp/l.jsonl");
        NX_CHECK(tracing.sink.rotation == tracing::RotationPolicy::Hourly);
        NX_CHECK(tracing.diagnostics.size() == 1U);
        NX_CHECK(tracing.diagnostics[0].field == "sink.rotation");
    }

    // A non-object sink keeps the whole previous sink and reports "sink".
    write_text(tracing_local, R"({"sink":"not-an-object"})");
    {
        app::TracingSettings tracing{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, tracing_local}, tracing)
                      .has_value());
        NX_CHECK(tracing.sink.path == "/tmp/g.jsonl");
        NX_CHECK(tracing.sink.rotation == tracing::RotationPolicy::Hourly);
        NX_CHECK(tracing.diagnostics[0].field == "sink");
    }

    // A non-object metrics sink reports "sink" and still populates provider fields.
    write_text(metrics_global, "{}");
    write_text(metrics_local,
               R"({"collect_interval_ns":42,"sink":"not-an-object"})");
    {
        app::MetricsSettings metrics{};
        const core::Status status =
            app::load_metrics_settings({metrics_global, metrics_local}, metrics);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
        NX_CHECK(metrics.options.collect_interval == std::chrono::nanoseconds{42});
        NX_CHECK(metrics.diagnostics.size() == 1U);
        NX_CHECK(metrics.diagnostics[0].field == "sink");
    }

    // Wrong field types keep the previous value and name the field.
    write_text(tracing_local,
               R"({"sink":{"max_spans_per_window":"x","window_ns":false}})");
    {
        app::TracingSettings tracing{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, tracing_local}, tracing)
                      .has_value());
        NX_CHECK(tracing.sink.max_spans_per_window == 0ULL);
        NX_CHECK(tracing.sink.window_ns == 0ULL);
        NX_CHECK(tracing.diagnostics.size() == 2U);
        NX_CHECK(tracing.diagnostics[0].field == "sink.max_spans_per_window");
        NX_CHECK(tracing.diagnostics[1].field == "sink.window_ns");
    }

    static_cast<void>(std::remove(metrics_global.c_str()));
    static_cast<void>(std::remove(metrics_local.c_str()));
    static_cast<void>(std::remove(tracing_global.c_str()));
    static_cast<void>(std::remove(tracing_local.c_str()));
}

void test_telemetry_layout() {
    Env_guard run_id_env{"NEXIGEN_RUN_ID"};
    Env_guard run_dir_env{"NEXIGEN_RUN_DIR"};
    Env_guard app_name_env{"NEXIGEN_APP_NAME"};
    Env_guard instance_id_env{"NEXIGEN_INSTANCE_ID"};
    Env_guard instance_dir_env{"NEXIGEN_INSTANCE_DIR"};
    run_id_env.clear();
    run_dir_env.clear();
    app_name_env.clear();
    instance_id_env.clear();
    instance_dir_env.clear();

    // Unset environment yields an empty, valid identity.
    {
        const app::RunIdentity empty = app::load_run_identity();
        NX_CHECK(empty.run_id.empty());
        NX_CHECK(empty.run_dir.empty());
        NX_CHECK(empty.app_name.empty());
        NX_CHECK(empty.instance_id.empty());
        NX_CHECK(empty.instance_dir.empty());
        NX_CHECK(app::validate_run_identity(empty).has_value());
    }

    // Partial environment preserves only the supplied fields.
    app_name_env.set("controller");
    instance_id_env.set("i42");
    {
        const app::RunIdentity partial = app::load_run_identity();
        NX_CHECK(partial.run_id.empty());
        NX_CHECK(partial.run_dir.empty());
        NX_CHECK(partial.app_name == "controller");
        NX_CHECK(partial.instance_id == "i42");
        NX_CHECK(partial.instance_dir.empty());
    }

    // Complete environment derives the instance directory.
    run_id_env.set("r1");
    run_dir_env.set("/var/nexigen/runs/r1");
    {
        const app::RunIdentity derived = app::load_run_identity();
        NX_CHECK(derived.run_id == "r1");
        NX_CHECK(derived.run_dir == "/var/nexigen/runs/r1");
        NX_CHECK(derived.instance_dir == "/var/nexigen/runs/r1/controller/i42");
        NX_CHECK(app::validate_run_identity(derived).has_value());
    }

    // A trailing slash on the run directory does not double the separator.
    run_dir_env.set("/var/nexigen/runs/r1/");
    {
        const app::RunIdentity derived = app::load_run_identity();
        NX_CHECK(derived.instance_dir == "/var/nexigen/runs/r1/controller/i42");
    }
    run_dir_env.set("/var/nexigen/runs/r1");

    // An explicit instance directory wins over derivation.
    instance_dir_env.set("/tmp/explicit-instance");
    {
        const app::RunIdentity explicit_identity = app::load_run_identity();
        NX_CHECK(explicit_identity.instance_dir == "/tmp/explicit-instance");
    }
    instance_dir_env.clear();

    // Validation: safe ids and absolute directories pass.
    const app::RunIdentity valid{"r1", "/var/nexigen/runs/r1", "controller", "i42",
                                  "/var/nexigen/runs/r1/controller/i42"};
    NX_CHECK(app::validate_run_identity(valid).has_value());

    // Unsafe ids and relative directories are rejected.
    {
        app::RunIdentity bad = valid;
        bad.app_name = "con/troller";
        const core::Status status = app::validate_run_identity(bad);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
    }
    {
        app::RunIdentity bad = valid;
        bad.instance_id = "i..42";
        NX_CHECK(!app::validate_run_identity(bad).has_value());
    }
    {
        app::RunIdentity bad = valid;
        bad.run_id = "..";
        NX_CHECK(!app::validate_run_identity(bad).has_value());
    }
    {
        app::RunIdentity bad = valid;
        bad.run_id = std::string(129, 'a');
        NX_CHECK(!app::validate_run_identity(bad).has_value());
    }
    {
        app::RunIdentity bad = valid;
        bad.run_dir = "relative/runs";
        NX_CHECK(!app::validate_run_identity(bad).has_value());
    }
    {
        app::RunIdentity bad = valid;
        bad.instance_dir = "relative/instance";
        NX_CHECK(!app::validate_run_identity(bad).has_value());
    }

    // Per-signal precedence: explicit path, instance fallback, then empty.
    {
        app::TelemetryPaths paths{};
        NX_CHECK(app::resolve_telemetry_paths(valid, "", "", "", paths).has_value());
        NX_CHECK(paths.metrics == "/var/nexigen/runs/r1/controller/i42/metrics.jsonl");
        NX_CHECK(paths.traces == "/var/nexigen/runs/r1/controller/i42/traces.jsonl");
        NX_CHECK(paths.chrono == "/var/nexigen/runs/r1/controller/i42/chrono.jsonl");

        NX_CHECK(app::resolve_telemetry_paths(valid, "/explicit/metrics.jsonl", "", "",
                                              paths)
                     .has_value());
        NX_CHECK(paths.metrics == "/explicit/metrics.jsonl");
        NX_CHECK(paths.traces == "/var/nexigen/runs/r1/controller/i42/traces.jsonl");
        NX_CHECK(paths.chrono == "/var/nexigen/runs/r1/controller/i42/chrono.jsonl");

        NX_CHECK(app::resolve_telemetry_paths(valid, "", "/explicit/traces.jsonl",
                                              "/explicit/chrono.jsonl", paths)
                     .has_value());
        NX_CHECK(paths.metrics == "/var/nexigen/runs/r1/controller/i42/metrics.jsonl");
        NX_CHECK(paths.traces == "/explicit/traces.jsonl");
        NX_CHECK(paths.chrono == "/explicit/chrono.jsonl");
    }

    // The settings overload reads each sink path.
    {
        app::MetricsSettings metrics{};
        app::TracingSettings tracing{};
        chrono::Chrono_settings chrono{};
        metrics.sink.path = "/settings/metrics.jsonl";
        tracing.sink.path = "";
        chrono.sink.path = "/settings/chrono.jsonl";
        app::TelemetryPaths paths{};
        NX_CHECK(app::resolve_telemetry_paths(valid, metrics, tracing, chrono, paths)
                     .has_value());
        NX_CHECK(paths.metrics == "/settings/metrics.jsonl");
        NX_CHECK(paths.traces == "/var/nexigen/runs/r1/controller/i42/traces.jsonl");
        NX_CHECK(paths.chrono == "/settings/chrono.jsonl");
    }

    // Empty identity and no explicit paths yield empty paths.
    {
        app::TelemetryPaths paths{};
        NX_CHECK(app::resolve_telemetry_paths(app::RunIdentity{}, "", "", "", paths)
                     .has_value());
        NX_CHECK(paths.metrics.empty() && paths.traces.empty() && paths.chrono.empty());
    }

    // Invalid identity is rejected before output is touched.
    {
        app::RunIdentity bad = valid;
        bad.instance_id = "bad id";
        app::TelemetryPaths untouched{"keep-metrics", "keep-traces", "keep-chrono"};
        const core::Status status =
            app::resolve_telemetry_paths(bad, "/x", "", "", untouched);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
        NX_CHECK(untouched.metrics == "keep-metrics");
        NX_CHECK(untouched.traces == "keep-traces");
        NX_CHECK(untouched.chrono == "keep-chrono");
    }

    // Resolution is pure: a non-existent instance directory is not created.
    {
        const std::string missing =
            unique_path("telemetry_layout_missing") + "/controller/i1";
        app::RunIdentity absent{"r1", "/tmp", "controller", "i1", missing};
        app::TelemetryPaths paths{};
        NX_CHECK(app::resolve_telemetry_paths(absent, "", "", "", paths).has_value());
        NX_CHECK(paths.metrics == missing + "/metrics.jsonl");
        NX_CHECK(::access(missing.c_str(), F_OK) != 0);
    }

    // Serialization round-trip and formatter equality.
    {
        nlohmann::json json;
        app::to_json(json, valid);
        app::RunIdentity back{};
        app::from_json(json, back);
        NX_CHECK(back.run_id == valid.run_id && back.run_dir == valid.run_dir);
        NX_CHECK(back.app_name == valid.app_name && back.instance_id == valid.instance_id);
        NX_CHECK(back.instance_dir == valid.instance_dir);
        NX_CHECK(std::format("{}", valid) == valid.to_string());
        NX_CHECK(app::to_string(valid) == valid.to_string());

        app::from_json(nlohmann::json::array(), back);
        NX_CHECK(back.run_id == valid.run_id);
        app::from_json(nlohmann::json{{"run_id", 5}, {"extra", true}}, back);
        NX_CHECK(back.run_id == valid.run_id);
        NX_CHECK(back.instance_dir == valid.instance_dir);

        app::TelemetryPaths paths{"/m", "/t", "/c"};
        nlohmann::json paths_json;
        app::to_json(paths_json, paths);
        app::TelemetryPaths paths_back{};
        app::from_json(paths_json, paths_back);
        NX_CHECK(paths_back.metrics == "/m" && paths_back.traces == "/t" &&
                 paths_back.chrono == "/c");
        NX_CHECK(std::format("{}", paths) == paths.to_string());

        app::from_json(nlohmann::json::array(), paths);
        NX_CHECK(paths.metrics == "/m");
        app::from_json(nlohmann::json{{"metrics", 1}, {"traces", false}}, paths);
        NX_CHECK(paths.metrics == "/m" && paths.traces == "/t");
    }
}

void test_telemetry_layout_infrastructure() {
    Env_guard run_id_env{"NEXIGEN_RUN_ID"};
    Env_guard run_dir_env{"NEXIGEN_RUN_DIR"};
    Env_guard app_name_env{"NEXIGEN_APP_NAME"};
    Env_guard instance_id_env{"NEXIGEN_INSTANCE_ID"};
    Env_guard instance_dir_env{"NEXIGEN_INSTANCE_DIR"};
    run_id_env.set("run-layout");
    run_dir_env.set("/tmp/opencode");
    app_name_env.set("layout-app");
    instance_id_env.set("i1");

    const std::string instance_dir = unique_path("telemetry_instance");
    make_dir(instance_dir);
    instance_dir_env.set(instance_dir.c_str());

    const app::RunIdentity identity = app::load_run_identity();
    NX_CHECK(identity.instance_dir == instance_dir);

    app::AppSettings settings{};
    NX_CHECK(app::load_app_settings(app::AppSettingsPaths{}, settings).has_value());
    settings.logging.options.log_root_directory = instance_dir;
    settings.logging.options.app_name = "layout-app";

    app::TelemetryPaths resolved{};
    NX_CHECK(app::resolve_telemetry_paths(identity, settings.metrics, settings.tracing,
                                          settings.chrono, resolved)
                 .has_value());
    NX_CHECK(resolved.metrics == instance_dir + "/metrics.jsonl");
    NX_CHECK(resolved.traces == instance_dir + "/traces.jsonl");
    NX_CHECK(resolved.chrono == instance_dir + "/chrono.jsonl");
    settings.metrics.sink.path = resolved.metrics;
    settings.tracing.sink.path = resolved.traces;
    settings.chrono.sink.path = resolved.chrono;

    auto infra = make_test_infra<Resolved_path_policy>();
    NX_CHECK(infra != nullptr);
    if (infra == nullptr) {
        return;
    }
    NX_CHECK(infra->initialize(settings).has_value());
    infra->logger().info("layout integration record");

    const auto meter = infra->meter("layout", "1.0");
    NX_CHECK(meter.has_value());
    if (meter.has_value()) {
        const auto counter =
            (*meter)->create_counter<metrics::ConcurrencyPolicy::MultiThreaded,
                                     metrics::AccessPolicy::Readable>("ticks");
        NX_CHECK(counter.has_value());
        if (counter.has_value()) {
            (*counter)->inc();
        }
    }
    NX_CHECK(infra->metrics().collect_now().has_value());
    NX_CHECK(infra->chrono().snapshot_now().has_value());

    const auto tracer = infra->tracer_for("layout.span");
    NX_CHECK(tracer.has_value());
    if (tracer.has_value()) {
        auto span = (*tracer)->start_span("work", tracing::SpanKind::Internal);
        static_cast<void>(span.set_status(tracing::SpanStatus::Ok));
        static_cast<void>(span.end());
    }
    NX_CHECK(infra->shutdown().has_value());

    const auto non_empty = [](const std::string& path) {
        struct stat st {};
        return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
    };
    NX_CHECK(non_empty(instance_dir + "/app.jsonl"));
    NX_CHECK(non_empty(resolved.metrics));
    NX_CHECK(non_empty(resolved.traces));
    NX_CHECK(non_empty(resolved.chrono));

    // Every artefact lives directly under the instance directory.
    DIR* const dir = ::opendir(instance_dir.c_str());
    NX_CHECK(dir != nullptr);
    if (dir != nullptr) {
        int entries = 0;
        while (const dirent* const entry = ::readdir(dir)) {
            const std::string_view name{entry->d_name};
            if (name == "." || name == "..") {
                continue;
            }
            ++entries;
            const bool known = name == "app.jsonl" || name == "metrics.jsonl" ||
                               name == "traces.jsonl" || name == "chrono.jsonl";
            NX_CHECK(known);
        }
        NX_CHECK(entries == 4);
        static_cast<void>(::closedir(dir));
    }

    static_cast<void>(std::remove((instance_dir + "/app.jsonl").c_str()));
    static_cast<void>(std::remove(resolved.metrics.c_str()));
    static_cast<void>(std::remove(resolved.traces.c_str()));
    static_cast<void>(std::remove(resolved.chrono.c_str()));
    static_cast<void>(::rmdir(instance_dir.c_str()));
}

void test_infrastructure_lifecycle() {
    auto app = make_test_infra<Test_policy>();
    NX_CHECK(app != nullptr);
    if (!app) {
        return;
    }

    NX_CHECK(!app->meter("sim", "1.0").has_value());
    NX_CHECK(app->meter("sim", "1.0").error() == core::ErrorCode::NotInitialized);
    NX_CHECK(!app->tracer_for("sim").has_value());
    NX_CHECK(app->tracer_for("sim").error() == core::ErrorCode::NotInitialized);

    NX_CHECK(app->initialize(app::AppSettings{}).has_value());
    NX_CHECK(app->initialize(app::AppSettings{}).has_value()); // idempotent

    g_log_lines.clear();
    app->logger().info("lifecycle record");
    NX_CHECK(g_log_lines.size() == 1U);

    NX_CHECK(app->chrono().snapshot_now().has_value());

    const auto meter = app->meter("sim", "1.0");
    NX_CHECK(meter.has_value());
    const auto counter = (*meter)->create_counter<metrics::ConcurrencyPolicy::SingleThreaded,
                                                  metrics::AccessPolicy::Readable>("ticks");
    NX_CHECK(counter.has_value());
    (*counter)->inc();
    (*counter)->inc();
    NX_CHECK((*counter)->read() == 2ULL);

    const auto tracer = app->tracer_for("sim.control");
    NX_CHECK(tracer.has_value());
    {
        auto span = (*tracer)->start_span("tick", tracing::SpanKind::Internal);
        NX_CHECK(span.set_attribute("n", 1));
    }

    int ticks = 0;
    const app::InfrastructureStopReason reason = app->run([&] {
        ++ticks;
        if (ticks == 3) {
            app->request_stop(app::InfrastructureStopReason::Requested);
        }
    });
    NX_CHECK(reason == app::InfrastructureStopReason::Requested);
    NX_CHECK(ticks == 3);

    NX_CHECK(app->shutdown().has_value());
    NX_CHECK(app->shutdown().has_value()); // idempotent
    int logged = 0;
    for (const std::string& line : g_log_lines) {
        if (line.find("shutdown reason: requested") != std::string::npos) {
            ++logged;
        }
    }
    NX_CHECK(logged == 1);
    NX_CHECK(!app->meter("x", "1").has_value());
}

void test_infrastructure_failures() {
    auto bool_app = make_test_infra<Test_policy>();
    NX_CHECK(bool_app != nullptr);
    if (bool_app) {
        NX_CHECK(bool_app->initialize(app::AppSettings{}).has_value());
        NX_CHECK(bool_app->run([] { return false; }) ==
                 app::InfrastructureStopReason::Requested);
        NX_CHECK(bool_app->shutdown().has_value());
    }

    auto fail_logger = make_test_infra<Fail_logger_policy>();
    NX_CHECK(fail_logger != nullptr);
    if (fail_logger) {
        NX_CHECK(!fail_logger->initialize(app::AppSettings{}).has_value());
        NX_CHECK(fail_logger->shutdown().has_value());
    }
    auto fail_metrics = make_test_infra<Fail_metrics_policy>();
    NX_CHECK(fail_metrics != nullptr);
    if (fail_metrics) {
        NX_CHECK(!fail_metrics->initialize(app::AppSettings{}).has_value());
        NX_CHECK(fail_metrics->shutdown().has_value());
    }
    auto fail_trace = make_test_infra<Fail_trace_policy>();
    NX_CHECK(fail_trace != nullptr);
    if (fail_trace) {
        NX_CHECK(!fail_trace->initialize(app::AppSettings{}).has_value());
        NX_CHECK(fail_trace->shutdown().has_value());
    }
    auto fail_chrono = make_test_infra<Fail_chrono_policy>();
    NX_CHECK(fail_chrono != nullptr);
    if (fail_chrono) {
        NX_CHECK(!fail_chrono->initialize(app::AppSettings{}).has_value());
        NX_CHECK(fail_chrono->shutdown().has_value());
    }
    auto flush_fail = make_test_infra<Flush_fail_policy>();
    NX_CHECK(flush_fail != nullptr);
    if (flush_fail) {
        NX_CHECK(flush_fail->initialize(app::AppSettings{}).has_value());
        const core::Status status = flush_fail->shutdown();
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::SystemError);
    }
}

void test_infrastructure_signals() {
    auto int_app = make_test_infra<Test_policy>();
    NX_CHECK(int_app != nullptr);
    if (!int_app) {
        return;
    }
    NX_CHECK(int_app->initialize(app::AppSettings{}).has_value());
    g_log_lines.clear();
    std::atomic<bool> running{false};
    std::jthread raiser{[&running] {
        while (!running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        static_cast<void>(::kill(::getpid(), SIGINT));
    }};
    const app::InfrastructureStopReason reason = int_app->run([&running] {
        running.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    });
    NX_CHECK(reason == app::InfrastructureStopReason::Sigint);
    NX_CHECK(int_app->shutdown().has_value());
    raiser.join();
    int logged = 0;
    for (const std::string& line : g_log_lines) {
        if (line.find("shutdown reason: sigint (2)") != std::string::npos) {
            ++logged;
        }
    }
    NX_CHECK(logged == 1);

    auto term_app = make_test_infra<Test_policy>();
    NX_CHECK(term_app != nullptr);
    if (!term_app) {
        return;
    }
    NX_CHECK(term_app->initialize(app::AppSettings{}).has_value());
    std::atomic<bool> term_running{false};
    std::jthread term_raiser{[&term_running] {
        while (!term_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        static_cast<void>(::kill(::getpid(), SIGTERM));
    }};
    const app::InfrastructureStopReason term_reason = term_app->run([&term_running] {
        term_running.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    });
    NX_CHECK(term_reason == app::InfrastructureStopReason::Sigterm);
    NX_CHECK(term_app->shutdown().has_value());
    term_raiser.join();
}

void test_infrastructure_tick_allocation() {
    auto infra = make_test_infra<Test_policy>();
    NX_CHECK(infra != nullptr);
    if (!infra) {
        return;
    }
    NX_CHECK(infra->initialize(app::AppSettings{}).has_value());
    int ticks = 0;
    [[maybe_unused]] const std::size_t before = ::mallinfo2().uordblks;
    const app::InfrastructureStopReason reason = infra->run([&] {
        ++ticks;
        if (ticks == 1000) {
            infra->request_stop(app::InfrastructureStopReason::Requested);
        }
    });
    [[maybe_unused]] const std::size_t after = ::mallinfo2().uordblks;
    NX_CHECK(reason == app::InfrastructureStopReason::Requested);
#ifndef NX_SANITIZER_BUILD
    NX_CHECK(after == before);
#endif
    NX_CHECK(infra->shutdown().has_value());
}

void test_app_settings_full_coverage() {
    Env_guard interval_env{"NEXIGEN_METRICS_COLLECT_INTERVAL_NS"};
    Env_guard metrics_mask_env{"NEXIGEN_METRICS_CORE_MASK"};
    Env_guard trace_mask_env{"NEXIGEN_TRACING_CORE_MASK"};
    Env_guard tracers_env{"NEXIGEN_TRACING_MAX_TRACERS"};
    Env_guard spans_env{"NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW"};
    Env_guard window_env{"NEXIGEN_TRACING_WINDOW_NS"};
    interval_env.clear(); metrics_mask_env.clear(); trace_mask_env.clear();
    tracers_env.clear(); spans_env.clear(); window_env.clear();

    const std::string base = unique_path("app_settings_full");
    const std::string metrics_global = base + "_mg.json";
    const std::string metrics_local = base + "_ml.json";
    const std::string tracing_global = base + "_tg.json";
    const std::string tracing_local = base + "_tl.json";

    write_text(metrics_global,
               R"({"collect_interval_ns":123456,"core_mask":3,)"
               R"("max_meters":1,"max_counters":2,"max_gauges":3,)"
               R"("max_statistics":4,"max_explicit_histograms":5,)"
               R"("max_exponential_histograms":6})");
    write_text(tracing_global,
               R"({"core_mask":7,"max_tracers":64,)"
               R"("max_spans_per_window":77,"window_ns":88})");
    {
        app::MetricsSettings metrics{};
        NX_CHECK(app::load_metrics_settings({metrics_global, ""}, metrics).has_value());
        NX_CHECK(metrics.options.collect_interval == std::chrono::nanoseconds{123456});
        NX_CHECK(metrics.options.core_mask == 3ULL);
        NX_CHECK(metrics.options.max_meters == 1U);
        NX_CHECK(metrics.options.max_counters == 2U);
        NX_CHECK(metrics.options.max_gauges == 3U);
        NX_CHECK(metrics.options.max_statistics == 4U);
        NX_CHECK(metrics.options.max_explicit_histograms == 5U);
        NX_CHECK(metrics.options.max_exponential_histograms == 6U);
        app::TracingSettings tracing{};
        NX_CHECK(app::load_tracing_settings({tracing_global, ""}, tracing).has_value());
        NX_CHECK(tracing.options.core_mask == 7ULL);
        NX_CHECK(tracing.options.max_tracers == 64U);
        NX_CHECK(tracing.options.max_spans_per_window == 77ULL);
        NX_CHECK(tracing.options.window_ns == 88ULL);
    }

    // Every metrics key wrong type -> diagnostics, values retained.
    write_text(metrics_local,
               R"({"collect_interval_ns":"x","core_mask":"x","max_meters":"x",)"
               R"("max_counters":"x","max_gauges":"x","max_statistics":"x",)"
               R"("max_explicit_histograms":"x","max_exponential_histograms":"x"})");
    {
        app::MetricsSettings metrics{};
        NX_CHECK(!app::load_metrics_settings({metrics_global, metrics_local}, metrics)
                      .has_value());
        NX_CHECK(metrics.diagnostics.size() >= 8U);
        NX_CHECK(metrics.options.collect_interval == std::chrono::nanoseconds{123456});
    }

    // Tracing invalid values: type errors, max_tracers 0/65, overflow.
    write_text(tracing_local,
               R"({"core_mask":"x","max_tracers":65,)"
               R"("max_spans_per_window":"x","window_ns":"x"})");
    {
        app::TracingSettings tracing{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, tracing_local}, tracing)
                      .has_value());
        NX_CHECK(tracing.diagnostics.size() >= 4U);
    }
    write_text(tracing_local, R"({"max_tracers":0})");
    {
        app::TracingSettings tracing{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, tracing_local}, tracing)
                      .has_value());
    }
    write_text(metrics_local,
               R"({"collect_interval_ns":18446744073709551615})");
    {
        app::MetricsSettings metrics{};
        NX_CHECK(!app::load_metrics_settings({metrics_global, metrics_local}, metrics)
                      .has_value());
    }

    // Unreadable paths on both subsystems.
    {
        app::MetricsSettings metrics{};
        const core::Status status = app::load_metrics_settings({"/tmp/opencode", ""}, metrics);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::SystemError);
        const core::Status local_metrics =
            app::load_metrics_settings({metrics_global, "/tmp/opencode"}, metrics);
        NX_CHECK(!local_metrics.has_value());
        NX_CHECK(local_metrics.error() == core::ErrorCode::SystemError);
        app::TracingSettings tracing{};
        const core::Status tracing_status =
            app::load_tracing_settings({"", "/tmp/opencode"}, tracing);
        NX_CHECK(!tracing_status.has_value());
        NX_CHECK(tracing_status.error() == core::ErrorCode::SystemError);
        const core::Status local_tracing =
            app::load_tracing_settings({tracing_global, "/tmp/opencode"}, tracing);
        NX_CHECK(!local_tracing.has_value());
        NX_CHECK(local_tracing.error() == core::ErrorCode::SystemError);
    }

    // Environment: every variable, valid and invalid.
    interval_env.set("50000000");
    metrics_mask_env.set("9");
    trace_mask_env.set("10");
    tracers_env.set("32");
    spans_env.set("100");
    window_env.set("200");
    {
        app::MetricsSettings metrics{};
        app::TracingSettings tracing{};
        NX_CHECK(app::load_metrics_settings({"", ""}, metrics).has_value());
        NX_CHECK(app::load_tracing_settings({"", ""}, tracing).has_value());
        NX_CHECK(metrics.options.collect_interval == std::chrono::nanoseconds{50'000'000});
        NX_CHECK(metrics.options.core_mask == 9ULL);
        NX_CHECK(tracing.options.core_mask == 10ULL);
        NX_CHECK(tracing.options.max_tracers == 32U);
        NX_CHECK(tracing.options.max_spans_per_window == 100ULL);
        NX_CHECK(tracing.options.window_ns == 200ULL);
    }
    interval_env.set("x");
    metrics_mask_env.set("x");
    trace_mask_env.set("x");
    tracers_env.set("65");
    spans_env.set("x");
    window_env.set("x");
    {
        app::MetricsSettings metrics{};
        app::TracingSettings tracing{};
        NX_CHECK(!app::load_metrics_settings({"", ""}, metrics).has_value());
        NX_CHECK(!app::load_tracing_settings({"", ""}, tracing).has_value());
        NX_CHECK(metrics.diagnostics.size() >= 2U);
        NX_CHECK(tracing.diagnostics.size() >= 4U);
    }
    interval_env.clear(); metrics_mask_env.clear(); trace_mask_env.clear();
    tracers_env.clear(); spans_env.clear(); window_env.clear();

    // Serialization of all four aggregate types.
    app::AppSettings aggregate{};
    aggregate.metrics = app::MetricsSettings{};
    aggregate.tracing = app::TracingSettings{};
    aggregate.logging = logging::Logging_settings{};
    const std::string metrics_text = aggregate.metrics.to_string();
    NX_CHECK(metrics_text.find("MetricsSettings") != std::string::npos);
    NX_CHECK(std::format("{}", aggregate.metrics) == metrics_text);
    const std::string tracing_text = aggregate.tracing.to_string();
    NX_CHECK(tracing_text.find("TracingSettings") != std::string::npos);
    NX_CHECK(std::format("{}", aggregate.tracing) == tracing_text);
    const app::AppSettingsPaths path_texts{
        {"g-log", "l-log"}, {"g-met", "l-met"}, {"g-tra", "l-tra"}};
    NX_CHECK(path_texts.to_string().find("g-met") != std::string::npos);
    NX_CHECK(std::format("{}", path_texts).find("g-tra") != std::string::npos);
    NX_CHECK(aggregate.to_string().find("AppSettings") != std::string::npos);
    NX_CHECK(std::format("{}", aggregate).find("Logging_settings") != std::string::npos);
    nlohmann::json json;
    app::to_json(json, aggregate);
    NX_CHECK(json.contains("logging") && json.contains("metrics") && json.contains("tracing"));
    app::to_json(json, path_texts);
    NX_CHECK(json["metrics"]["global_file"] == "g-met");

    // Aggregate worst status: clean logging + unreadable metrics.
    app::AppSettingsPaths worst{};
    worst.metrics.global_file = "/tmp/opencode";
    app::AppSettings loaded{};
    const core::Status worst_status = app::load_app_settings(worst, loaded);
    NX_CHECK(!worst_status.has_value());
    NX_CHECK(worst_status.error() == core::ErrorCode::SystemError);

    static_cast<void>(std::remove(metrics_global.c_str()));
    static_cast<void>(std::remove(metrics_local.c_str()));
    static_cast<void>(std::remove(tracing_global.c_str()));
    static_cast<void>(std::remove(tracing_local.c_str()));
}

void test_infrastructure_conflict() {
    auto first = make_test_infra<Test_policy>();
    auto second = make_test_infra<Test_policy>();
    NX_CHECK(first != nullptr && second != nullptr);
    if (!first || !second) {
        return;
    }
    NX_CHECK(first->initialize(app::AppSettings{}).has_value());
    const core::Status conflict = second->initialize(app::AppSettings{});
    NX_CHECK(!conflict.has_value());
    NX_CHECK(conflict.error() == core::ErrorCode::Conflict);
    NX_CHECK(first->shutdown().has_value());
    NX_CHECK(second->initialize(app::AppSettings{}).has_value());
    NX_CHECK(second->shutdown().has_value());

    // A failed initialization releases ownership for the next infrastructure.
    auto failing = make_test_infra<Fail_logger_policy>();
    NX_CHECK(failing != nullptr);
    if (failing) {
        NX_CHECK(!failing->initialize(app::AppSettings{}).has_value());
        auto after_failure = make_test_infra<Test_policy>();
        NX_CHECK(after_failure != nullptr);
        if (after_failure) {
            NX_CHECK(after_failure->initialize(app::AppSettings{}).has_value());
            NX_CHECK(after_failure->shutdown().has_value());
        }
    }

    // Different policies still conflict on the process-global guard.
    auto trace_first = make_test_infra<Test_policy>();
    auto trace_second = make_test_infra<Fail_trace_policy>();
    NX_CHECK(trace_first != nullptr && trace_second != nullptr);
    if (trace_first && trace_second) {
        NX_CHECK(trace_first->initialize(app::AppSettings{}).has_value());
        const core::Status policy_conflict = trace_second->initialize(app::AppSettings{});
        NX_CHECK(!policy_conflict.has_value());
        NX_CHECK(policy_conflict.error() == core::ErrorCode::Conflict);
        NX_CHECK(trace_first->shutdown().has_value());
        NX_CHECK(trace_second->shutdown().has_value());
    }
}

void test_infrastructure_stop_and_mask() {
    {
        auto first = make_test_infra<Test_policy>();
        NX_CHECK(first != nullptr);
        if (first) {
            NX_CHECK(first->initialize(app::AppSettings{}).has_value());
            first->request_stop(app::InfrastructureStopReason::None);
            NX_CHECK(!first->stop_requested());
            first->request_stop(app::InfrastructureStopReason::Requested);
            first->request_stop(app::InfrastructureStopReason::Sigint);
            NX_CHECK(first->stop_reason() == app::InfrastructureStopReason::Requested);
            NX_CHECK(first->shutdown().has_value());
        }
    }
    {
        auto second = make_test_infra<Test_policy>();
        NX_CHECK(second != nullptr);
        if (second) {
            NX_CHECK(second->initialize(app::AppSettings{}).has_value());
            second->request_stop(app::InfrastructureStopReason::Sigint);
            second->request_stop(app::InfrastructureStopReason::Requested);
            NX_CHECK(second->stop_reason() == app::InfrastructureStopReason::Sigint);
            NX_CHECK(second->shutdown().has_value());
        }
    }

    auto masked = make_test_infra<Test_policy>();
    NX_CHECK(masked != nullptr);
    if (!masked) {
        return;
    }
    NX_CHECK(masked->initialize(app::AppSettings{}).has_value());
    std::atomic<bool> both_blocked{false};
    std::jthread probe{[&] {
        sigset_t current;
        sigemptyset(&current);
        if (::pthread_sigmask(SIG_BLOCK, nullptr, &current) == 0) {
            both_blocked.store(::sigismember(&current, SIGINT) == 1 &&
                                   ::sigismember(&current, SIGTERM) == 1,
                               std::memory_order_release);
        }
    }};
    probe.join();
    NX_CHECK(both_blocked.load(std::memory_order_acquire));
    NX_CHECK(masked->shutdown().has_value());
}

void test_command_line_parser() {
    // Long options: separate and '=' value forms.
    {
        auto parsed = parse_args({"--global-settings", "/etc/nexigen",
                                  "--local-settings", "./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/nexigen");
            NX_CHECK(parsed->local_settings_dir == "./hil-config");
            NX_CHECK(!parsed->log_level.has_value());
            NX_CHECK(!parsed->help);
        }
    }
    {
        auto parsed = parse_args({"--global-settings=/etc/nexigen",
                                  "--local-settings=./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/nexigen");
            NX_CHECK(parsed->local_settings_dir == "./hil-config");
        }
    }

    // Multi-character short names: separate, adjacent and '=' value forms.
    {
        auto parsed = parse_args({"-gs", "/etc/nexigen", "-ls", "./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/nexigen");
            NX_CHECK(parsed->local_settings_dir == "./hil-config");
        }
    }
    {
        auto parsed = parse_args({"-gs/etc/nexigen", "-ls./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/nexigen");
            NX_CHECK(parsed->local_settings_dir == "./hil-config");
        }
    }
    {
        auto parsed = parse_args({"-gs=/etc/nexigen", "-ls=./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/nexigen");
            NX_CHECK(parsed->local_settings_dir == "./hil-config");
        }
    }

    // Log-level short forms.
    for (const std::string_view spelling : {"-lgl info", "-lglinfo", "-lgl=info"}) {
        const std::size_t split = spelling.find(' ');
        const std::string_view token = spelling.substr(0, split);
        auto parsed = split == std::string_view::npos
                          ? parse_args({"-gs", "/etc/nexigen", token})
                          : parse_args({"-gs", "/etc/nexigen", token,
                                        spelling.substr(split + 1)});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->log_level.has_value());
            NX_CHECK(parsed->log_level == logging::LogLevel::Info);
        }
    }

    // Every canonical level spelling.
    struct Level_case {
        std::string_view name;
        logging::LogLevel level;
    };
    constexpr Level_case kLevels[] = {
        {"verbose", logging::LogLevel::Verbose},
        {"debug", logging::LogLevel::Debug},
        {"info", logging::LogLevel::Info},
        {"warning", logging::LogLevel::Warning},
        {"error", logging::LogLevel::Error},
        {"fatal", logging::LogLevel::Fatal},
        {"off", logging::LogLevel::Off},
    };
    for (const Level_case& level : kLevels) {
        auto parsed = parse_args({"-gs", "/etc/nexigen", "-lgl", level.name});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->log_level.has_value());
            NX_CHECK(parsed->log_level == level.level);
        }
    }

    // Unknown and mixed-case levels are rejected with the accepted list.
    for (const std::string_view bad_level : {"chatty", "INFO", ""}) {
        auto parsed = parse_args({"-gs", "/etc/nexigen", "--log-level", bad_level});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--log-level"));
            NX_CHECK(message_contains(parsed.error(), "verbose"));
            NX_CHECK(message_contains(parsed.error(), "off"));
        }
    }

    // Single-letter aliases are not part of the contract.
    for (const std::string_view alias : {"-g", "-l", "-L"}) {
        auto parsed = parse_args({alias, "/etc/nexigen"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), alias));
        }
    }

    // argv[0] is the program name and is skipped.
    {
        const char* const argv[] = {"my-app", "--help"};
        auto parsed = app::parse_command_line(2, argv);
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->help);
        }
        const char* const dirs[] = {"my-app", "-gs", "/etc/nexigen"};
        auto from_argv = app::parse_command_line(3, dirs);
        NX_CHECK(from_argv.has_value());
        if (from_argv.has_value()) {
            NX_CHECK(from_argv->global_settings_dir == "/etc/nexigen");
        }
        auto no_arguments = app::parse_command_line(1, argv);
        NX_CHECK(!no_arguments.has_value());
        auto null_argv = app::parse_command_line(0, nullptr);
        NX_CHECK(!null_argv.has_value());
    }

    // Help wins over every other argument and never requires a directory.
    for (const std::string_view help_token : {"-h", "--help"}) {
        auto parsed = parse_args({help_token});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->help);
        }
    }
    {
        auto parsed = parse_args({"--colour", "--global-settings", "./relative",
                                  "--help"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->help);
        }
    }

    // Terminator: following tokens are positional; a bare terminator is fine.
    {
        auto parsed = parse_args({"--local-settings", "./config", "--"});
        NX_CHECK(parsed.has_value());
    }
    {
        auto parsed = parse_args({"--local-settings", "./config", "--", "extra"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "extra"));
        }
    }
    {
        auto parsed = parse_args({"--local-settings", "./config", "--", "--help"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--help"));
        }
    }

    // Unknown, missing, repeated and positional arguments.
    {
        auto parsed = parse_args({"--colour"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--colour"));
        }
    }
    {
        auto parsed = parse_args({"--global-settings"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--global-settings"));
        }
    }
    {
        auto parsed = parse_args({"--global-settings", "--local-settings", "./config"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--global-settings"));
        }
    }
    {
        auto parsed = parse_args({"-gs", "/etc/a", "-gs", "/etc/b", "-ls", "./x",
                                  "-lgl", "debug", "-lgl", "error"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/b");
            NX_CHECK(parsed->log_level == logging::LogLevel::Error);
        }
    }
    {
        auto parsed = parse_args({"config"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "config"));
        }
    }
    {
        auto parsed = parse_args({"-"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "-"));
        }
    }

    // Directory shape validation and empty values.
    {
        auto parsed = parse_args({"--global-settings", "./config"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--global-settings"));
        }
    }
    {
        auto parsed = parse_args({"--local-settings", "/etc/nexigen"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--local-settings"));
        }
    }
    {
        auto parsed = parse_args({"--global-settings=", "--local-settings", "./x"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--global-settings"));
        }
    }
    {
        auto parsed = parse_args({"--global-settings", "", "--local-settings", "./x"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--global-settings"));
        }
    }
    {
        auto parsed = parse_args({"--local-settings=", "--global-settings",
                                  "/etc/nexigen"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--local-settings"));
        }
    }

    // At least one directory is required, except for help.
    {
        auto parsed = parse_args({});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(),
                                    "--global-settings/--local-settings"));
            NX_CHECK(message_contains(parsed.error(),
                                      "at least one settings directory is required"));
        }
    }
    {
        auto parsed = parse_args({"--global-settings", "/etc/nexigen"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->local_settings_dir.empty());
        }
    }
    {
        auto parsed = parse_args({"--local-settings", "./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir.empty());
        }
    }

    // Usage text names the whole contract.
    const std::string_view usage = app::command_line_usage();
    for (const std::string_view token : {"-gs", "--global-settings", "-ls",
                                         "--local-settings", "-lgl", "--log-level",
                                         "-h", "--help", "verbose", "off",
                                         "logging.json", "metrics.json",
                                         "tracing.json", "chrono.json"}) {
        NX_CHECK(usage.find(token) != std::string_view::npos);
    }
}

void test_command_line_mapping() {
    app::CommandLineOptions options{};
    options.global_settings_dir = "/etc/nexigen";
    options.local_settings_dir = "./hil-config";
    app::AppSettingsPaths paths = app::make_app_settings_paths(options);
    NX_CHECK(paths.logging.global_file == "/etc/nexigen/logging.json");
    NX_CHECK(paths.logging.local_file == "./hil-config/logging.json");
    NX_CHECK(paths.metrics.global_file == "/etc/nexigen/metrics.json");
    NX_CHECK(paths.metrics.local_file == "./hil-config/metrics.json");
    NX_CHECK(paths.tracing.global_file == "/etc/nexigen/tracing.json");
    NX_CHECK(paths.tracing.local_file == "./hil-config/tracing.json");
    NX_CHECK(paths.chrono.global_file == "/etc/nexigen/chrono.json");
    NX_CHECK(paths.chrono.local_file == "./hil-config/chrono.json");

    options.global_settings_dir = "/etc/nexigen/";
    paths = app::make_app_settings_paths(options);
    NX_CHECK(paths.logging.global_file == "/etc/nexigen/logging.json");
    NX_CHECK(paths.metrics.global_file == "/etc/nexigen/metrics.json");
    NX_CHECK(paths.tracing.global_file == "/etc/nexigen/tracing.json");
    NX_CHECK(paths.chrono.global_file == "/etc/nexigen/chrono.json");
    NX_CHECK(paths.logging.global_file.find("//") == std::string::npos);

    options.global_settings_dir.clear();
    paths = app::make_app_settings_paths(options);
    NX_CHECK(paths.logging.global_file.empty());
    NX_CHECK(paths.metrics.global_file.empty());
    NX_CHECK(paths.tracing.global_file.empty());
    NX_CHECK(paths.chrono.global_file.empty());
    NX_CHECK(paths.logging.local_file == "./hil-config/logging.json");

    app::AppSettings settings{};
    settings.logging.options.initial_log_level = logging::LogLevel::Debug;
    options.log_level = logging::LogLevel::Warning;
    app::apply_to(options, settings);
    NX_CHECK(settings.logging.options.initial_log_level == logging::LogLevel::Warning);

    options.log_level.reset();
    settings.logging.options.initial_log_level = logging::LogLevel::Error;
    app::apply_to(options, settings);
    NX_CHECK(settings.logging.options.initial_log_level == logging::LogLevel::Error);
}

void test_command_line_serialization() {
    const app::CommandLineDiagnostic diagnostic{"--log-level", "bad level"};
    NX_CHECK(app::to_string(diagnostic) == diagnostic.to_string());
    NX_CHECK(std::format("{}", diagnostic) == diagnostic.to_string());
    NX_CHECK(diagnostic.to_string().find("--log-level") != std::string::npos);

    app::CommandLineOptions options{};
    options.global_settings_dir = "/etc/nexigen";
    options.local_settings_dir = "./hil-config";
    options.log_level = logging::LogLevel::Warning;
    options.help = true;
    NX_CHECK(app::to_string(options) == options.to_string());
    NX_CHECK(std::format("{}", options) == options.to_string());
    NX_CHECK(options.to_string().find("warning") != std::string::npos);

    const app::CommandLineError error{core::ErrorCode::InvalidArgument,
                                        {diagnostic}};
    NX_CHECK(app::to_string(error) == error.to_string());
    NX_CHECK(std::format("{}", error) == error.to_string());
    NX_CHECK(error.to_string().find("InvalidArgument") != std::string::npos);

    nlohmann::json diagnostic_json;
    app::to_json(diagnostic_json, diagnostic);
    NX_CHECK(diagnostic_json["option"] == "--log-level");
    NX_CHECK(diagnostic_json["message"] == "bad level");
    app::CommandLineDiagnostic recovered_diagnostic{};
    app::from_json(diagnostic_json, recovered_diagnostic);
    NX_CHECK(recovered_diagnostic.option == diagnostic.option);
    NX_CHECK(recovered_diagnostic.message == diagnostic.message);

    app::CommandLineDiagnostic unchanged_diagnostic{"keep", "keep"};
    app::from_json(nlohmann::json::array(), unchanged_diagnostic);
    NX_CHECK(unchanged_diagnostic.option == "keep");
    app::from_json(nlohmann::json{{"option", 7}}, unchanged_diagnostic);
    NX_CHECK(unchanged_diagnostic.option == "keep");
    app::from_json(nlohmann::json{{"message", "new"}}, unchanged_diagnostic);
    NX_CHECK(unchanged_diagnostic.option.empty());
    NX_CHECK(unchanged_diagnostic.message == "new");

    app::CommandLineDiagnostic defaulted_diagnostic{"keep", "keep"};
    app::from_json(nlohmann::json::object(), defaulted_diagnostic);
    NX_CHECK(defaulted_diagnostic.option.empty());
    NX_CHECK(defaulted_diagnostic.message.empty());

    nlohmann::json options_json;
    app::to_json(options_json, options);
    NX_CHECK(options_json["global_settings_dir"] == "/etc/nexigen");
    NX_CHECK(options_json["local_settings_dir"] == "./hil-config");
    NX_CHECK(options_json["log_level"] == "warning");
    NX_CHECK(options_json["help"] == true);
    app::CommandLineOptions recovered_options{};
    app::from_json(options_json, recovered_options);
    NX_CHECK(recovered_options.global_settings_dir == "/etc/nexigen");
    NX_CHECK(recovered_options.local_settings_dir == "./hil-config");
    NX_CHECK(recovered_options.log_level == logging::LogLevel::Warning);
    NX_CHECK(recovered_options.help);

    app::CommandLineOptions defaulted_options{};
    defaulted_options.global_settings_dir = "/keep";
    defaulted_options.local_settings_dir = "./keep";
    defaulted_options.log_level = logging::LogLevel::Warning;
    defaulted_options.help = true;
    app::from_json(nlohmann::json::object(), defaulted_options);
    NX_CHECK(defaulted_options.global_settings_dir.empty());
    NX_CHECK(defaulted_options.local_settings_dir.empty());
    NX_CHECK(defaulted_options.log_level == logging::LogLevel::Info);
    NX_CHECK(!defaulted_options.help);

    app::CommandLineOptions partial_options{};
    app::from_json(nlohmann::json{{"global_settings_dir", "/only"}}, partial_options);
    NX_CHECK(partial_options.global_settings_dir == "/only");
    NX_CHECK(partial_options.local_settings_dir.empty());
    NX_CHECK(partial_options.log_level == logging::LogLevel::Info);
    NX_CHECK(!partial_options.help);

    app::CommandLineOptions kept_options{};
    kept_options.global_settings_dir = "/keep";
    kept_options.log_level = logging::LogLevel::Warning;
    app::from_json(nlohmann::json{{"help", "yes"}}, kept_options);
    NX_CHECK(kept_options.global_settings_dir == "/keep");
    NX_CHECK(kept_options.log_level == logging::LogLevel::Warning);
    app::from_json(nlohmann::json{{"log_level", "chatty"}}, kept_options);
    NX_CHECK(kept_options.global_settings_dir == "/keep");
    NX_CHECK(kept_options.log_level == logging::LogLevel::Warning);

    app::CommandLineOptions non_object_options{};
    non_object_options.global_settings_dir = "/keep";
    non_object_options.log_level = logging::LogLevel::Warning;
    app::from_json(nlohmann::json::array(), non_object_options);
    NX_CHECK(non_object_options.global_settings_dir == "/keep");
    NX_CHECK(non_object_options.log_level == logging::LogLevel::Warning);

    options.log_level.reset();
    app::to_json(options_json, options);
    NX_CHECK(options_json["log_level"].is_null());
    app::from_json(options_json, recovered_options);
    NX_CHECK(!recovered_options.log_level.has_value());

    nlohmann::json error_json;
    app::to_json(error_json, error);
    NX_CHECK(error_json["code"] == "InvalidArgument");
    NX_CHECK(error_json["diagnostics"].size() == 1U);
    app::CommandLineError recovered_error{};
    app::from_json(error_json, recovered_error);
    NX_CHECK(recovered_error.code == core::ErrorCode::InvalidArgument);
    NX_CHECK(recovered_error.diagnostics.size() == 1U);
    NX_CHECK(recovered_error.diagnostics[0].option == "--log-level");
    NX_CHECK(recovered_error.diagnostics[0].message == "bad level");

    app::CommandLineError unchanged_error{core::ErrorCode::Conflict, {diagnostic}};
    app::from_json(nlohmann::json(7), unchanged_error);
    NX_CHECK(unchanged_error.code == core::ErrorCode::Conflict);
    NX_CHECK(unchanged_error.diagnostics.size() == 1U);
    app::from_json(nlohmann::json{{"code", 7}}, unchanged_error);
    NX_CHECK(unchanged_error.code == core::ErrorCode::Conflict);
    app::from_json(nlohmann::json{{"diagnostics", "nope"}}, unchanged_error);
    NX_CHECK(unchanged_error.diagnostics.size() == 1U);
    app::from_json(nlohmann::json{{"diagnostics", nlohmann::json::array({7})}},
                   unchanged_error);
    NX_CHECK(unchanged_error.diagnostics.size() == 1U);
    app::from_json(
        nlohmann::json{{"diagnostics", nlohmann::json::array({{{"option", 7}}})}},
        unchanged_error);
    NX_CHECK(unchanged_error.diagnostics.size() == 1U);
    NX_CHECK(unchanged_error.diagnostics[0].option == "--log-level");

    app::CommandLineError defaulted_error{core::ErrorCode::Conflict, {diagnostic}};
    app::from_json(nlohmann::json::object(), defaulted_error);
    NX_CHECK(defaulted_error.code == core::ErrorCode::InvalidArgument);
    NX_CHECK(defaulted_error.diagnostics.empty());

    app::CommandLineError partial_error{core::ErrorCode::Conflict, {diagnostic}};
    app::from_json(
        nlohmann::json{{"diagnostics", nlohmann::json::array({{{"message", "m"}}})}},
        partial_error);
    NX_CHECK(partial_error.code == core::ErrorCode::InvalidArgument);
    NX_CHECK(partial_error.diagnostics.size() == 1U);
    NX_CHECK(partial_error.diagnostics[0].option.empty());
    NX_CHECK(partial_error.diagnostics[0].message == "m");
}

void test_command_line_loading() {
    Env_guard log_level_env{"NEXIGEN_LOG_LEVEL"};
    log_level_env.clear();

    const std::string global_dir = unique_path("app_cli_global");
    const std::string local_dir = unique_path("app_cli_local");
    make_dir(global_dir);
    make_dir(local_dir);
    write_text(global_dir + "/logging.json", R"({"initial_log_level":"debug"})");
    write_text(local_dir + "/logging.json", R"({"initial_log_level":"error"})");

    app::CommandLineOptions options{};
    options.global_settings_dir = global_dir;
    options.log_level = logging::LogLevel::Warning;
    app::AppSettings loaded{};
    core::Status status = app::load_app_settings(options, loaded);
    NX_CHECK(status.has_value());
    NX_CHECK(loaded.logging.global_loaded);
    NX_CHECK(!loaded.logging.local_loaded);
    NX_CHECK(loaded.logging.options.initial_log_level == logging::LogLevel::Warning);

    options.local_settings_dir = local_dir;
    status = app::load_app_settings(options, loaded);
    NX_CHECK(status.has_value());
    NX_CHECK(loaded.logging.global_loaded);
    NX_CHECK(loaded.logging.local_loaded);
    NX_CHECK(loaded.logging.options.initial_log_level == logging::LogLevel::Warning);

    // A bad file is best-effort: the CLI level still wins and diagnostics stay.
    write_text(global_dir + "/logging.json", "{");
    app::AppSettings failed{};
    status = app::load_app_settings(options, failed);
    NX_CHECK(!status.has_value());
    if (!status.has_value()) {
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
    }
    NX_CHECK(!failed.logging.diagnostics.empty());
    NX_CHECK(failed.logging.options.initial_log_level == logging::LogLevel::Warning);

    // Manually built options with no directory cannot load built-in defaults.
    app::CommandLineOptions empty{};
    app::AppSettings untouched{};
    status = app::load_app_settings(empty, untouched);
    NX_CHECK(!status.has_value());
    if (!status.has_value()) {
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
    }

    // Local-only options leave the global source disabled.
    app::CommandLineOptions local_only{};
    local_only.local_settings_dir = local_dir;
    app::AppSettings local_loaded{};
    status = app::load_app_settings(local_only, local_loaded);
    NX_CHECK(status.has_value());
    NX_CHECK(!local_loaded.logging.global_loaded);
    NX_CHECK(local_loaded.logging.local_loaded);

    static_cast<void>(std::remove((global_dir + "/logging.json").c_str()));
    static_cast<void>(std::remove((local_dir + "/logging.json").c_str()));
    static_cast<void>(::rmdir(global_dir.c_str()));
    static_cast<void>(::rmdir(local_dir.c_str()));
}

void test_command_line_infrastructure() {
    Env_guard log_level_env{"NEXIGEN_LOG_LEVEL"};
    log_level_env.clear();

    const std::string dir = unique_path("app_cli_infra");
    make_dir(dir);
    write_text(dir + "/logging.json", R"({"initial_log_level":"debug"})");

    app::CommandLineOptions options{};
    options.global_settings_dir = dir;
    options.log_level = logging::LogLevel::Warning;

    auto infra = make_test_infra<Test_policy>();
    NX_CHECK(infra != nullptr);
    if (infra) {
        app::AppSettings loaded{};
        const core::Status status = infra->initialize(options, loaded);
        NX_CHECK(status.has_value());
        NX_CHECK(loaded.logging.global_loaded);
        NX_CHECK(loaded.logging.options.initial_log_level ==
                 logging::LogLevel::Warning);
        NX_CHECK(infra->meter("sim", "1.0").has_value());
        NX_CHECK(infra->shutdown().has_value());
    }

    // Fail fast on a loader error; loaded keeps best-effort data and a later
    // explicit initialize(loaded) opts into degraded startup.
    write_text(dir + "/logging.json", "{");
    auto failing = make_test_infra<Test_policy>();
    NX_CHECK(failing != nullptr);
    if (failing) {
        app::AppSettings failed{};
        const core::Status status = failing->initialize(options, failed);
        NX_CHECK(!status.has_value());
        if (!status.has_value()) {
            NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
        }
        NX_CHECK(!failing->meter("x", "1").has_value());
        NX_CHECK(!failed.logging.diagnostics.empty());
        NX_CHECK(failed.logging.options.initial_log_level ==
                 logging::LogLevel::Warning);
        NX_CHECK(failing->initialize(failed).has_value());
        NX_CHECK(failing->meter("x", "1").has_value());
        NX_CHECK(failing->shutdown().has_value());
    }

    // Help is a caller action, not an initialization request.
    auto help_infra = make_test_infra<Test_policy>();
    NX_CHECK(help_infra != nullptr);
    if (help_infra) {
        app::CommandLineOptions help_options{};
        help_options.help = true;
        app::AppSettings help_loaded{};
        const core::Status status = help_infra->initialize(help_options, help_loaded);
        NX_CHECK(!status.has_value());
        if (!status.has_value()) {
            NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
        }
        NX_CHECK(!help_infra->meter("x", "1").has_value());
        NX_CHECK(help_infra->shutdown().has_value());
    }

    // A manually built options value with no directory is rejected too.
    auto empty_infra = make_test_infra<Test_policy>();
    NX_CHECK(empty_infra != nullptr);
    if (empty_infra) {
        app::CommandLineOptions empty{};
        app::AppSettings empty_loaded{};
        const core::Status status = empty_infra->initialize(empty, empty_loaded);
        NX_CHECK(!status.has_value());
        if (!status.has_value()) {
            NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
        }
        NX_CHECK(!empty_infra->meter("x", "1").has_value());
        NX_CHECK(empty_infra->shutdown().has_value());
    }

    // The pre-existing settings overload remains unchanged.
    auto direct = make_test_infra<Test_policy>();
    NX_CHECK(direct != nullptr);
    if (direct) {
        NX_CHECK(direct->initialize(app::AppSettings{}).has_value());
        NX_CHECK(direct->shutdown().has_value());
    }

    static_cast<void>(std::remove((dir + "/logging.json").c_str()));
    static_cast<void>(::rmdir(dir.c_str()));
}

int main() {
    run_suite("stop_reason", test_stop_reason);
    run_suite("command_line", test_command_line_parser);
    run_suite("cli_mapping", test_command_line_mapping);
    run_suite("cli_serialization", test_command_line_serialization);
    run_suite("cli_loading", test_command_line_loading);
    run_suite("cli_infrastructure", test_command_line_infrastructure);
    run_suite("settings", test_settings_loaders);
    run_suite("settings_sink", test_settings_sink);
    run_suite("settings_full", test_app_settings_full_coverage);
    run_suite("telemetry_layout", test_telemetry_layout);
    run_suite("telemetry_infra", test_telemetry_layout_infrastructure);
    run_suite("lifecycle", test_infrastructure_lifecycle);
    run_suite("failures", test_infrastructure_failures);
    run_suite("conflict", test_infrastructure_conflict);
    run_suite("stop_and_mask", test_infrastructure_stop_and_mask);
    run_suite("signals", test_infrastructure_signals);
    run_suite("tick_allocation", test_infrastructure_tick_allocation);
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
