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
#include <nexigen/chrono/json_file_sink.hpp>
#include <nexigen/chrono/null_sink.hpp>
#include <nexigen/chrono/detail/time_base_segment.hpp>
#include <nexigen/chrono/detail/time_base_seqlock.hpp>
#include <nexigen/chrono/time_base_host_domain.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/chrono/utc_offset.hpp>
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

    /// @brief Quiescence is trivial: records are captured synchronously.
    [[nodiscard]] core::Status quiesce() noexcept { return {}; }

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
    using Sink = chrono::NullSink;

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
};

/// Counts metric batches without retaining them (provider quiescence tests).
struct CountingMetricSink {
    /// Batches observed since the last explicit reset.
    static inline std::atomic<std::uint32_t> batches{0};

    [[nodiscard]] core::Status on_event(const metrics::MetricBatchSnapshot&) noexcept {
        batches.fetch_add(1U, std::memory_order_relaxed);
        return {};
    }
    [[nodiscard]] core::Status flush() noexcept { return {}; }
    [[nodiscard]] std::string to_string() const { return "CountingMetricSink"; }
};

static_assert(metrics::concepts::MetricSink<CountingMetricSink>);

/// Test policy whose metric sink counts batches across shutdown/destruction.
struct CountingPolicy {
    using Logger = CaptureLogger;
    using MetricsSink = CountingMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using Sink = chrono::NullSink;

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
};

/// A metric sink whose flush always fails (quiescence-failure logging tests).
struct FailingFlushMetricSink {
    [[nodiscard]] core::Status on_event(const metrics::MetricBatchSnapshot&) noexcept { return {}; }
    [[nodiscard]] core::Status flush() noexcept {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    [[nodiscard]] std::string to_string() const { return "FailingFlushMetricSink"; }
};

static_assert(metrics::concepts::MetricSink<FailingFlushMetricSink>);

/// Logger recording the control-plane call sequence (ordering tests).
class SequencedLogger {
public:
    /// Events appended in call order: `log:<text>`, `flush`, `quiesce`.
    static inline std::vector<std::string> events;

    void set_level(core::LogLevel level) noexcept { level_ = level; }
    [[nodiscard]] core::LogLevel level() const noexcept { return level_; }

    template <class... Args>
    void log(core::LogLevel, logging::detail::format_param_t<Args...> fmt,
             Args&&... args) noexcept {
        events.push_back("log:" + std::vformat(fmt.fmt.get(), std::make_format_args(args...)));
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

    [[nodiscard]] core::Status flush() noexcept {
        events.push_back("flush");
        return {};
    }
    [[nodiscard]] core::Status quiesce() noexcept {
        events.push_back("quiesce");
        return {};
    }

private:
    core::LogLevel level_{core::LogLevel::Info};
};

static_assert(logging::concepts::Logger<SequencedLogger>);
static_assert(logging::concepts::QuiescableLogger<SequencedLogger>);

struct FailingMetricSinkPolicy {
    using Logger = CaptureLogger;
    using MetricsSink = FailingFlushMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using Sink = chrono::NullSink;

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
};

struct OrderedQuiescePolicy {
    using Logger = SequencedLogger;
    using MetricsSink = FailingFlushMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using Sink = chrono::NullSink;

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
};

/// File-backed policy used by the telemetry-layout integration test. It reads
/// every sink from the settings value and never uses a hard-coded path.
struct ResolvedPathPolicy {
    using Logger = logging::FileLogger<>;
    using MetricsSink = metrics::JsonFileMetricSink;
    using TraceSink = tracing::JsonFileTraceSink;
    using Sink = chrono::JsonFileSink;

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
};

struct RtPolicy {
    using Logger = logging::FileLogger<128, 128>;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using Sink = chrono::NullSink;

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
};

struct FailLoggerPolicy {
    using Logger = CaptureLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using Sink = chrono::NullSink;

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
};

struct FailMetricsPolicy {
    using Logger = CaptureLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using Sink = chrono::NullSink;

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
};

struct FailTracePolicy {
    using Logger = CaptureLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using Sink = chrono::NullSink;

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
};

struct FlushFailPolicy {
    using Logger = FlushFailLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using Sink = chrono::NullSink;

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
};

struct FailChronoPolicy {
    using Logger = CaptureLogger;
    using MetricsSink = metrics::NullMetricSink;
    using TraceSink = tracing::NullTraceSink;
    using Sink = chrono::NullSink;

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
};

template <class Policy> using TestInfra = app::Infrastructure<Policy, 64, 4>;

template <class Policy> [[nodiscard]] std::unique_ptr<TestInfra<Policy>> make_test_infra() {
    auto created = app::make_infrastructure<Policy, 64, 4>();
    NX_CHECK(created.has_value());
    return created.has_value() ? std::move(*created) : nullptr;
}

// --- default time-base fixture ---------------------------------------------
// Every application test initializes an Infrastructure, which now requires a
// published time base. This fixture owns the production default segment name
// for the test process: the first test binary creates it, and concurrent
// binaries attach to the same published block. The heartbeat is published far
// in the future so the client never enters holdover during an unrelated test;
// the dedicated time-base suites exercise the real windows with their own
// segments.

class DefaultTimeBaseFixture {
public:
    DefaultTimeBaseFixture() {
        auto created = chrono::detail::TimeBaseSegment::create(std::string(k_name), 0x7465U);
        if (created) {
            segment_.emplace(std::move(*created));
            publish();
            return;
        }
        if (created.error() == core::ErrorCode::Conflict) {
            auto attached = chrono::detail::TimeBaseSegment::attach(std::string(k_name));
            if (attached) {
                segment_.emplace(std::move(*attached));
            }
        }
    }

    /// @brief The segment name the fixture publishes under.
    [[nodiscard]] static constexpr std::string_view name() noexcept { return k_name; }

private:
    void publish() {
        const auto now = chrono::Timestamp::now();
        if (!now) {
            return;
        }
        const std::uint64_t mono = now->nanoseconds();
        chrono::TimeBaseBlock block{};
        block.status = chrono::TimeBaseStatus::Ready;
        block.boot_token = 0x7465U;
        block.domain_count = 3U;
        std::int64_t utc_offset = 0;
        static_cast<void>(chrono::sample_utc_offset(utc_offset));
        block.utc_offset_ns = utc_offset;
        block.rows[0].domain_id = chrono::k_monotonic_raw_domain_id;
        block.rows[0].domain_class = chrono::TimeDomainClass::SystemClock;
        block.rows[0].lock = chrono::LockState::Locked;
        block.rows[0].scale_fwd_q32 = 1ULL << 32U;
        block.rows[0].native_anchor = mono;
        block.rows[0].canonical_anchor_ns = static_cast<std::int64_t>(mono);
        block.rows[0].valid_from_native = mono;
        block.rows[0].valid_to_native = mono + 10'000'000'000ULL;
        block.rows[0].valid_from_canonical = mono;
        block.rows[0].valid_to_canonical = mono + 10'000'000'000ULL;
        block.rows[1].domain_id = chrono::k_host_counter_domain_id;
        block.rows[1].domain_class = chrono::TimeDomainClass::CycleCounter;
        block.rows[1].lock = chrono::LockState::Locked;
        block.rows[1].epoch = 1U;
        block.rows[1].scale_fwd_q32 = 1ULL << 32U;
        block.rows[1].native_anchor = mono * 3U;
        block.rows[1].canonical_anchor_ns = static_cast<std::int64_t>(mono);
        block.rows[1].valid_from_native = mono * 3U;
        block.rows[1].valid_to_native = (mono + 10'000'000'000ULL) * 3U;
        block.rows[1].valid_from_canonical = mono;
        block.rows[1].valid_to_canonical = mono + 10'000'000'000ULL;
        block.rows[1].scale_rev_q32 = 1ULL << 32U;
        block.rows[1].flags = chrono::DomainCoefficient::has_reverse_bit;
        block.rows[2].domain_id = chrono::k_realtime_domain_id;
        block.rows[2].domain_class = chrono::TimeDomainClass::SystemClock;
        block.rows[2].lock = chrono::LockState::Locked;
        block.rows[2].epoch = 1U;
        block.rows[2].native_anchor =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(mono) + utc_offset);
        block.rows[2].canonical_anchor_ns = static_cast<std::int64_t>(mono);
        block.rows[2].scale_fwd_q32 = 1ULL << 32U;
        block.rows[2].scale_rev_q32 = 1ULL << 32U;
        block.rows[2].flags = chrono::DomainCoefficient::has_reverse_bit;
        block.rows[2].valid_from_native = block.rows[2].native_anchor;
        block.rows[2].valid_to_native = block.rows[2].native_anchor + 3'600'000'000'000ULL;
        block.rows[2].valid_from_canonical = mono;
        block.rows[2].valid_to_canonical = mono + 3'600'000'000'000ULL;
        chrono::detail::TimeBaseWriter writer{*segment_->block()};
        writer.publish(block, mono + 3'600'000'000'000ULL);
    }

    static constexpr std::string_view k_name = "/nexigen-timebase";
    std::optional<chrono::detail::TimeBaseSegment> segment_{};
};

inline DefaultTimeBaseFixture g_default_time_base{};

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
