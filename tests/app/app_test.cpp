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
    std::jthread raiser{[] {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        static_cast<void>(::kill(::getpid(), SIGINT));
    }};
    const app::App_stop_reason reason = int_app.run([] {
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
    std::jthread term_raiser{[] {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        static_cast<void>(::kill(::getpid(), SIGTERM));
    }};
    const app::App_stop_reason term_reason = term_app.run([] {
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

int main() {
    run_suite("stop_reason", test_stop_reason);
    run_suite("settings", test_settings_loaders);
    run_suite("lifecycle", test_app_lifecycle);
    run_suite("bool_tick_failure", test_app_bool_tick_and_failure);
    run_suite("signals", test_app_signals);
    run_suite("tick_allocation", test_app_tick_allocation);
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
