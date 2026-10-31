// nexigen::app tests — zero-dependency harness (same NX_CHECK/run_suite
// style as the other modules), change add-app-class.
//
// Covers the stop-reason contract, per-subsystem settings loaders, the App
// lifecycle (initialize/run/shutdown/current), the metric and tracer factory
// accessors, rollback on a failing hook, and real SIGINT/SIGTERM delivery.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <malloc.h>

#include <nexigen/app/app.hpp>
#include <nexigen/app/app_settings.hpp>
#include <nexigen/app/app_stop_reason.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>
#include <nexigen/tracing/null_trace_sink.hpp>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define NX_SANITIZER_BUILD 1
#endif

namespace {

namespace app = nexigen::app;
namespace core = nexigen::core;
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

class Test_app;
using Test_app_base =
    app::App<Test_app, Capture_logger, metrics::NullMetricSink,
             tracing::NullTraceSink, 64, 4>;

class Test_app final : public Test_app_base {
public:
    [[nodiscard]] core::Status emplace_logger(
        std::optional<Capture_logger>& out,
        const logging::Logging_settings&) noexcept {
        out.emplace();
        return {};
    }
    [[nodiscard]] core::Status emplace_metrics_sink(
        std::optional<metrics::NullMetricSink>& out,
        const app::Metrics_settings&) noexcept {
        out.emplace();
        return {};
    }
    [[nodiscard]] core::Status emplace_trace_sink(
        std::optional<tracing::NullTraceSink>& out,
        const app::Tracing_settings&) noexcept {
        out.emplace();
        return {};
    }
};

class Failing_app;
using Failing_app_base =
    app::App<Failing_app, Capture_logger, metrics::NullMetricSink,
             tracing::NullTraceSink, 64, 4>;

class Failing_app final : public Failing_app_base {
public:
    [[nodiscard]] core::Status emplace_logger(
        std::optional<Capture_logger>& out,
        const logging::Logging_settings&) noexcept {
        out.emplace();
        return {};
    }
    [[nodiscard]] core::Status emplace_metrics_sink(
        std::optional<metrics::NullMetricSink>&,
        const app::Metrics_settings&) noexcept {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    [[nodiscard]] core::Status emplace_trace_sink(
        std::optional<tracing::NullTraceSink>& out,
        const app::Tracing_settings&) noexcept {
        out.emplace();
        return {};
    }
};

class Fail_logger_app final
    : public app::App<Fail_logger_app, Capture_logger, metrics::NullMetricSink,
                      tracing::NullTraceSink, 64, 4> {
public:
    [[nodiscard]] core::Status emplace_logger(
        std::optional<Capture_logger>&,
        const logging::Logging_settings&) noexcept {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    [[nodiscard]] core::Status emplace_metrics_sink(
        std::optional<metrics::NullMetricSink>& out,
        const app::Metrics_settings&) noexcept {
        out.emplace();
        return {};
    }
    [[nodiscard]] core::Status emplace_trace_sink(
        std::optional<tracing::NullTraceSink>& out,
        const app::Tracing_settings&) noexcept {
        out.emplace();
        return {};
    }
};

class Fail_flush_app final
    : public app::App<Fail_flush_app, Flush_fail_logger, metrics::NullMetricSink,
                      tracing::NullTraceSink, 64, 4> {
public:
    [[nodiscard]] core::Status emplace_logger(
        std::optional<Flush_fail_logger>& out,
        const logging::Logging_settings&) noexcept {
        out.emplace();
        return {};
    }
    [[nodiscard]] core::Status emplace_metrics_sink(
        std::optional<metrics::NullMetricSink>& out,
        const app::Metrics_settings&) noexcept {
        out.emplace();
        return {};
    }
    [[nodiscard]] core::Status emplace_trace_sink(
        std::optional<tracing::NullTraceSink>& out,
        const app::Tracing_settings&) noexcept {
        out.emplace();
        return {};
    }
};

class Fail_trace_sink_app final
    : public app::App<Fail_trace_sink_app, Capture_logger, metrics::NullMetricSink,
                      tracing::NullTraceSink, 64, 4> {
public:
    [[nodiscard]] core::Status emplace_logger(
        std::optional<Capture_logger>& out,
        const logging::Logging_settings&) noexcept {
        out.emplace();
        return {};
    }
    [[nodiscard]] core::Status emplace_metrics_sink(
        std::optional<metrics::NullMetricSink>& out,
        const app::Metrics_settings&) noexcept {
        out.emplace();
        return {};
    }
    [[nodiscard]] core::Status emplace_trace_sink(
        std::optional<tracing::NullTraceSink>&,
        const app::Tracing_settings&) noexcept {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
};

template <class T, class = void>
struct has_from_json : std::false_type {};
template <class T>
struct has_from_json<
    T, std::void_t<decltype(from_json(std::declval<const nlohmann::json&>(),
                                      std::declval<T&>()))>> : std::true_type {};

static_assert(!has_from_json<app::Metrics_settings>::value);
static_assert(!has_from_json<app::Tracing_settings>::value);
static_assert(!has_from_json<app::App_settings_paths>::value);
static_assert(!has_from_json<app::App_settings>::value);
static_assert(has_from_json<app::Settings_paths>::value);

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

} // namespace

void test_stop_reason() {
    NX_CHECK(app::to_string(app::App_stop_reason::None) == "none");
    NX_CHECK(app::to_string(app::App_stop_reason::Requested) == "requested");
    NX_CHECK(app::to_string(app::App_stop_reason::Sigint) == "sigint");
    NX_CHECK(app::to_string(app::App_stop_reason::Sigterm) == "sigterm");

    app::App_stop_reason reason = app::App_stop_reason::None;
    NX_CHECK(app::from_string("sigterm", reason));
    NX_CHECK(reason == app::App_stop_reason::Sigterm);
    NX_CHECK(!app::from_string("bogus", reason));
    NX_CHECK(reason == app::App_stop_reason::Sigterm);

    nlohmann::json json;
    app::to_json(json, app::App_stop_reason::Sigint);
    NX_CHECK(json == "sigint");
    app::from_json(nlohmann::json("requested"), reason);
    NX_CHECK(reason == app::App_stop_reason::Requested);
    app::from_json(nlohmann::json(7), reason);
    NX_CHECK(reason == app::App_stop_reason::Requested);
    NX_CHECK(std::format("{}", app::App_stop_reason::Sigterm) == "sigterm");
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

    app::Metrics_settings metrics{};
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

    app::Tracing_settings tracing{};
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
    app::App_settings app_settings{};
    app::App_settings_paths paths{};
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
        app::Metrics_settings metrics_invalid{};
        NX_CHECK(!app::load_metrics_settings({metrics_global, ""}, metrics_invalid).has_value());
    }
    write_text(tracing_global, "[]");
    {
        app::Tracing_settings tracing_invalid{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, ""}, tracing_invalid).has_value());
        NX_CHECK(!tracing_invalid.diagnostics.empty());
    }
    write_text(tracing_global, "{");
    {
        app::Tracing_settings tracing_invalid{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, ""}, tracing_invalid).has_value());
    }

    // Serialization with non-empty diagnostics (loop branches).
    {
        app::Metrics_settings metrics_serialized{};
        metrics_serialized.global_loaded = true;
        metrics_serialized.diagnostics.push_back(
            {logging::Settings_source::Local, "core_mask", "bad"});
        NX_CHECK(metrics_serialized.to_string().find("bad") != std::string::npos);
        nlohmann::json serialized_json;
        app::to_json(serialized_json, metrics_serialized);
        NX_CHECK(serialized_json["diagnostics"].size() == 1U);
        app::Tracing_settings tracing_serialized{};
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
        app::Metrics_settings metrics_local_bad{};
        NX_CHECK(!app::load_metrics_settings({metrics_global, metrics_local},
                                             metrics_local_bad)
                      .has_value());
        write_text(tracing_global, R"({"max_tracers":2})");
        write_text(tracing_local, "{");
        app::Tracing_settings tracing_local_bad{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, tracing_local},
                                             tracing_local_bad)
                      .has_value());
    }

    // Aggregate InvalidArgument when subsystems load but contain diagnostics.
    {
        write_text(metrics_local, R"({"core_mask":"x"})");
        write_text(tracing_local, R"({"max_tracers":0})");
        app::App_settings_paths aggregate_paths{};
        aggregate_paths.metrics = {metrics_global, metrics_local};
        aggregate_paths.tracing = {tracing_global, tracing_local};
        app::App_settings out{};
        const core::Status status = app::load_app_settings(aggregate_paths, out);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
    }

    static_cast<void>(std::remove(metrics_global.c_str()));
    static_cast<void>(std::remove(metrics_local.c_str()));
    static_cast<void>(std::remove(tracing_global.c_str()));
    static_cast<void>(std::remove(tracing_local.c_str()));
}

void test_app_lifecycle() {
    static Test_app app;
    const app::App_settings settings{};

    NX_CHECK(!app.meter("sim", "1.0").has_value());
    NX_CHECK(app.meter("sim", "1.0").error() == core::ErrorCode::NotInitialized);
    NX_CHECK(!app.tracer_for("sim").has_value());
    NX_CHECK(app.tracer_for("sim").error() == core::ErrorCode::NotInitialized);

    NX_CHECK(app.initialize(settings).has_value());
    NX_CHECK(app.initialize(settings).has_value()); // idempotent
    NX_CHECK(Test_app::current() == &app);

    g_log_lines.clear();
    app.logger().info("lifecycle record");
    NX_CHECK(g_log_lines.size() == 1U);

    const auto meter = app.meter("sim", "1.0");
    NX_CHECK(meter.has_value());
    const auto counter = (*meter)->create_counter<metrics::ConcurrencyPolicy::SingleThreaded,
                                                  metrics::AccessPolicy::Readable>("ticks");
    NX_CHECK(counter.has_value());
    (*counter)->inc();
    (*counter)->inc();
    NX_CHECK((*counter)->read() == 2ULL);

    const auto tracer = app.tracer_for("sim.control");
    NX_CHECK(tracer.has_value());
    {
        auto span = (*tracer)->start_span("tick", tracing::SpanKind::Internal);
        NX_CHECK(span.set_attribute("n", 1));
    }

    int ticks = 0;
    const app::App_stop_reason reason = app.run([&] {
        ++ticks;
        if (ticks == 3) {
            app.request_stop(app::App_stop_reason::Requested);
        }
    });
    NX_CHECK(reason == app::App_stop_reason::Requested);
    NX_CHECK(ticks == 3);

    NX_CHECK(app.shutdown().has_value());
    NX_CHECK(Test_app::current() == nullptr);
    NX_CHECK(app.shutdown().has_value()); // idempotent
    bool reason_logged = false;
    for (const std::string& line : g_log_lines) {
        if (line.find("shutdown reason: requested") != std::string::npos) {
            reason_logged = true;
        }
    }
    NX_CHECK(reason_logged);
    NX_CHECK(!app.meter("x", "1").has_value());
}

void test_app_bool_tick_and_failure() {
    static Test_app bool_app;
    NX_CHECK(bool_app.initialize(app::App_settings{}).has_value());
    const app::App_stop_reason reason = bool_app.run([] { return false; });
    NX_CHECK(reason == app::App_stop_reason::Requested);
    NX_CHECK(bool_app.shutdown().has_value());

    static Failing_app failing;
    NX_CHECK(!failing.initialize(app::App_settings{}).has_value());
    NX_CHECK(Failing_app::current() == nullptr);
    NX_CHECK(failing.shutdown().has_value());
}

void test_app_signals() {
    static Test_app int_app;
    NX_CHECK(int_app.initialize(app::App_settings{}).has_value());
    g_log_lines.clear();
    std::atomic<bool> int_running{false};
    std::jthread raiser{[&int_running] {
        while (!int_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        static_cast<void>(::kill(::getpid(), SIGINT));
    }};
    const app::App_stop_reason reason = int_app.run([&int_running] {
        int_running.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    });
    NX_CHECK(reason == app::App_stop_reason::Sigint);
    NX_CHECK(int_app.shutdown().has_value());
    int logged = 0;
    for (const std::string& line : g_log_lines) {
        if (line.find("shutdown reason: sigint (2)") != std::string::npos) {
            ++logged;
        }
    }
    NX_CHECK(logged == 1);

    static Test_app term_app;
    NX_CHECK(term_app.initialize(app::App_settings{}).has_value());
    std::atomic<bool> term_running{false};
    std::jthread term_raiser{[&term_running] {
        while (!term_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        static_cast<void>(::kill(::getpid(), SIGTERM));
    }};
    const app::App_stop_reason term_reason = term_app.run([&term_running] {
        term_running.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    });
    NX_CHECK(term_reason == app::App_stop_reason::Sigterm);
    NX_CHECK(term_app.shutdown().has_value());
}

void test_app_tick_allocation() {
    static Test_app app;
    NX_CHECK(app.initialize(app::App_settings{}).has_value());
    int ticks = 0;
    [[maybe_unused]] const std::size_t before = ::mallinfo2().uordblks;
    const app::App_stop_reason reason = app.run([&] {
        ++ticks;
        if (ticks == 1000) {
            app.request_stop(app::App_stop_reason::Requested);
        }
    });
    [[maybe_unused]] const std::size_t after = ::mallinfo2().uordblks;
    NX_CHECK(reason == app::App_stop_reason::Requested);
#ifndef NX_SANITIZER_BUILD
    NX_CHECK(after == before);
#endif
    NX_CHECK(app.shutdown().has_value());
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
        app::Metrics_settings metrics{};
        NX_CHECK(app::load_metrics_settings({metrics_global, ""}, metrics).has_value());
        NX_CHECK(metrics.options.collect_interval == std::chrono::nanoseconds{123456});
        NX_CHECK(metrics.options.core_mask == 3ULL);
        NX_CHECK(metrics.options.max_meters == 1U);
        NX_CHECK(metrics.options.max_counters == 2U);
        NX_CHECK(metrics.options.max_gauges == 3U);
        NX_CHECK(metrics.options.max_statistics == 4U);
        NX_CHECK(metrics.options.max_explicit_histograms == 5U);
        NX_CHECK(metrics.options.max_exponential_histograms == 6U);
        app::Tracing_settings tracing{};
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
        app::Metrics_settings metrics{};
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
        app::Tracing_settings tracing{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, tracing_local}, tracing)
                      .has_value());
        NX_CHECK(tracing.diagnostics.size() >= 4U);
    }
    write_text(tracing_local, R"({"max_tracers":0})");
    {
        app::Tracing_settings tracing{};
        NX_CHECK(!app::load_tracing_settings({tracing_global, tracing_local}, tracing)
                      .has_value());
    }
    write_text(metrics_local,
               R"({"collect_interval_ns":18446744073709551615})");
    {
        app::Metrics_settings metrics{};
        NX_CHECK(!app::load_metrics_settings({metrics_global, metrics_local}, metrics)
                      .has_value());
    }

    // Unreadable paths on both subsystems.
    {
        app::Metrics_settings metrics{};
        const core::Status status = app::load_metrics_settings({"/tmp/opencode", ""}, metrics);
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == core::ErrorCode::SystemError);
        const core::Status local_metrics =
            app::load_metrics_settings({metrics_global, "/tmp/opencode"}, metrics);
        NX_CHECK(!local_metrics.has_value());
        NX_CHECK(local_metrics.error() == core::ErrorCode::SystemError);
        app::Tracing_settings tracing{};
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
        app::Metrics_settings metrics{};
        app::Tracing_settings tracing{};
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
        app::Metrics_settings metrics{};
        app::Tracing_settings tracing{};
        NX_CHECK(!app::load_metrics_settings({"", ""}, metrics).has_value());
        NX_CHECK(!app::load_tracing_settings({"", ""}, tracing).has_value());
        NX_CHECK(metrics.diagnostics.size() >= 2U);
        NX_CHECK(tracing.diagnostics.size() >= 4U);
    }
    interval_env.clear(); metrics_mask_env.clear(); trace_mask_env.clear();
    tracers_env.clear(); spans_env.clear(); window_env.clear();

    // Serialization of all four aggregate types.
    app::App_settings aggregate{};
    aggregate.metrics = app::Metrics_settings{};
    aggregate.tracing = app::Tracing_settings{};
    aggregate.logging = logging::Logging_settings{};
    const std::string metrics_text = aggregate.metrics.to_string();
    NX_CHECK(metrics_text.find("Metrics_settings") != std::string::npos);
    NX_CHECK(std::format("{}", aggregate.metrics) == metrics_text);
    const std::string tracing_text = aggregate.tracing.to_string();
    NX_CHECK(tracing_text.find("Tracing_settings") != std::string::npos);
    NX_CHECK(std::format("{}", aggregate.tracing) == tracing_text);
    const app::App_settings_paths path_texts{
        {"g-log", "l-log"}, {"g-met", "l-met"}, {"g-tra", "l-tra"}};
    NX_CHECK(path_texts.to_string().find("g-met") != std::string::npos);
    NX_CHECK(std::format("{}", path_texts).find("g-tra") != std::string::npos);
    NX_CHECK(aggregate.to_string().find("App_settings") != std::string::npos);
    NX_CHECK(std::format("{}", aggregate).find("Logging_settings") != std::string::npos);
    nlohmann::json json;
    app::to_json(json, aggregate);
    NX_CHECK(json.contains("logging") && json.contains("metrics") && json.contains("tracing"));
    app::to_json(json, path_texts);
    NX_CHECK(json["metrics"]["global_file"] == "g-met");

    // Aggregate worst status: clean logging + unreadable metrics.
    app::App_settings_paths worst{};
    worst.metrics.global_file = "/tmp/opencode";
    app::App_settings loaded{};
    const core::Status worst_status = app::load_app_settings(worst, loaded);
    NX_CHECK(!worst_status.has_value());
    NX_CHECK(worst_status.error() == core::ErrorCode::SystemError);

    static_cast<void>(std::remove(metrics_global.c_str()));
    static_cast<void>(std::remove(metrics_local.c_str()));
    static_cast<void>(std::remove(tracing_global.c_str()));
    static_cast<void>(std::remove(tracing_local.c_str()));
}

void test_app_failure_variants() {
    static Fail_logger_app logger_fail;
    NX_CHECK(!logger_fail.initialize(app::App_settings{}).has_value());
    NX_CHECK(Fail_logger_app::current() == nullptr);
    NX_CHECK(logger_fail.shutdown().has_value());

    static Fail_trace_sink_app trace_fail;
    NX_CHECK(!trace_fail.initialize(app::App_settings{}).has_value());
    NX_CHECK(Fail_trace_sink_app::current() == nullptr);
    NX_CHECK(trace_fail.shutdown().has_value());

    static Fail_flush_app flush_fail;
    NX_CHECK(flush_fail.initialize(app::App_settings{}).has_value());
    const core::Status shutdown_status = flush_fail.shutdown();
    NX_CHECK(!shutdown_status.has_value());
    NX_CHECK(shutdown_status.error() == core::ErrorCode::SystemError);
    NX_CHECK(Fail_flush_app::current() == nullptr);
}

void test_app_stop_and_mask() {
    {
        static Test_app first;
        NX_CHECK(first.initialize(app::App_settings{}).has_value());
        first.request_stop(app::App_stop_reason::None);
        NX_CHECK(!first.stop_requested());
        first.request_stop(app::App_stop_reason::Requested);
        first.request_stop(app::App_stop_reason::Sigint);
        NX_CHECK(first.stop_reason() == app::App_stop_reason::Requested);
        NX_CHECK(first.shutdown().has_value());
    }
    {
        static Test_app second;
        NX_CHECK(second.initialize(app::App_settings{}).has_value());
        second.request_stop(app::App_stop_reason::Sigint);
        second.request_stop(app::App_stop_reason::Requested);
        NX_CHECK(second.stop_reason() == app::App_stop_reason::Sigint);
        NX_CHECK(second.shutdown().has_value());
    }

    static Test_app masked;
    NX_CHECK(masked.initialize(app::App_settings{}).has_value());
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
    NX_CHECK(masked.shutdown().has_value());
}

void test_app_sequential_current() {
    static Test_app first;
    static Test_app second;
    NX_CHECK(first.initialize(app::App_settings{}).has_value());
    NX_CHECK(Test_app::current() == &first);
    NX_CHECK(first.shutdown().has_value());
    NX_CHECK(Test_app::current() == nullptr);
    NX_CHECK(second.initialize(app::App_settings{}).has_value());
    NX_CHECK(Test_app::current() == &second);
    NX_CHECK(second.shutdown().has_value());
    NX_CHECK(Test_app::current() == nullptr);
    NX_CHECK(!second.initialize(app::App_settings{}).has_value());
}

int main() {
    run_suite("stop_reason", test_stop_reason);
    run_suite("settings", test_settings_loaders);
    run_suite("settings_full", test_app_settings_full_coverage);
    run_suite("lifecycle", test_app_lifecycle);
    run_suite("bool_tick_failure", test_app_bool_tick_and_failure);
    run_suite("failure_variants", test_app_failure_variants);
    run_suite("stop_and_mask", test_app_stop_and_mask);
    run_suite("sequential_current", test_app_sequential_current);
    run_suite("signals", test_app_signals);
    run_suite("tick_allocation", test_app_tick_allocation);
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
