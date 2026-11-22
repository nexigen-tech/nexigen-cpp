// nexigen::logging — logger quiescence (change
// keep-telemetry-handles-valid-until-infrastructure-destruction;
// spec logging/logger-quiescence).

#include "logging_test_support.hpp"

#include <nexigen/logging/composite_logger.hpp>
#include <nexigen/logging/concepts/quiescable_logger_concept.hpp>
#include <nexigen/logging/console_logger.hpp>
#include <nexigen/logging/detail/async_worker.hpp>
#include <nexigen/logging/detail/producer.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/null_logger.hpp>

namespace {

using nexigen::core::ErrorCode;
using nexigen::core::LogLevel;
using nexigen::logging::CompositeLogger;
using nexigen::logging::ConsoleLogger;
using nexigen::logging::FileLogger;
using nexigen::logging::FileLoggerConfig;
using nexigen::logging::NullLogger;
using nexigen::logging::concepts::QuiescableLogger;

static_assert(QuiescableLogger<NullLogger>, "NullLogger must quiesce");
static_assert(QuiescableLogger<ConsoleLogger<64, 96>>, "ConsoleLogger must quiesce");
static_assert(QuiescableLogger<FileLogger<64, 96>>, "FileLogger must quiesce");
static_assert(QuiescableLogger<CompositeLogger<NullLogger, NullLogger>>,
              "CompositeLogger must quiesce");

/// A type without `quiesce` must be rejected.
struct LoggerWithoutQuiesce {};

static_assert(!QuiescableLogger<LoggerWithoutQuiesce>,
              "a logger without quiesce must not satisfy the concept");

/// Per-run base directory for the file-logger checks.
const char* quiesce_base() {
    alignas(64) static char base[512];
    static const bool initialized = [] {
        std::snprintf(base, sizeof base, "/tmp/opencode/nexigen-quiesce-%d", ::getpid());
        return true;
    }();
    (void)initialized;
    return base;
}

/// NullLogger quiescence is trivially complete and repeatable.
void test_null_quiesce() {
    NullLogger logger{};
    NX_CHECK(logger.quiesce().has_value());
    NX_CHECK(logger.quiesce().has_value());
    NX_CHECK(logger.flush().has_value());
}

/// A quiesced console logger keeps the object alive and drops later records.
void test_console_quiesce() {
    ConsoleLogger<64, 96> logger{};
    ConsoleLogger<64, 96>::Config cfg{};
    cfg.initial_level = LogLevel::Info;
    NX_CHECK(logger.initialize(cfg).has_value());

    logger.info("before quiesce");
    const std::size_t allocations_before = g_alloc_used;
    NX_CHECK(logger.quiesce().has_value());
    NX_CHECK(g_alloc_used == allocations_before); // allocation-free quiescence

    // Terminal and idempotent: the barrier is gone, the second call is harmless.
    const auto after = logger.flush();
    NX_CHECK(!after.has_value() && after.error() == ErrorCode::NotInitialized);
    NX_CHECK(logger.quiesce().has_value());

    // Post-quiescence records are accepted into the ring and dropped when full;
    // no call blocks and the sink receives nothing more.
    const std::uint64_t drops_before = logger.dropped().buffer_full;
    for (int i = 0; i < 256; ++i) {
        logger.info("after quiesce {}", i);
    }
    NX_CHECK(logger.dropped().buffer_full > drops_before);
    NX_CHECK(logger.pending() <= 64U);
}

/// The file sink is closed at quiescence and receives no later record.
void test_file_quiesce() {
    const char* base = quiesce_base();
    FileLogger<64, 96> logger{};
    FileLoggerConfig cfg{};
    cfg.app_name = "quiesce";
    cfg.base_dir = base;
    cfg.file_name = "quiesce.jsonl";
    cfg.rotation.kind = nexigen::logging::RotationSchedule::None;
    cfg.initial_level = LogLevel::Info;
    NX_CHECK(logger.initialize(cfg).has_value());

    logger.info("delivered-before");
    NX_CHECK(logger.quiesce().has_value());
    NX_CHECK(logger.quiesce().has_value());

    char path[560];
    std::snprintf(path, sizeof path, "%s/quiesce.jsonl", base);
    char content[40096];
    const std::size_t size_after_quiesce = read_whole_file(path, content);
    NX_CHECK(size_after_quiesce != SIZE_MAX);
    NX_CHECK(std::string_view(content, size_after_quiesce).find("delivered-before") !=
             std::string_view::npos);

    // Records after quiescence never reach the closed sink.
    for (int i = 0; i < 256; ++i) {
        logger.info("after quiesce {}", i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const std::size_t size_later = read_whole_file(path, content);
    NX_CHECK(size_later == size_after_quiesce);
    NX_CHECK(!logger.flush().has_value());

    // Diagnostics stay available after quiescence.
    NX_CHECK(!logger.to_string().empty());
    NX_CHECK(logger.dropped().buffer_full > 0U);
}

/// A conforming logger whose quiesce can be made to fail, counting calls.
///
/// Two instantiations of the same type cannot be configured differently, so the
/// failure mode is a template parameter and the counters are static per
/// instantiation.
template <bool Fail> class CountingQuiesceLogger {
public:
    static inline int quiesce_calls = 0;

    void set_level(core::LogLevel level) noexcept { level_ = level; }
    [[nodiscard]] core::LogLevel level() const noexcept { return level_; }

    template <class... Args>
    void log(core::LogLevel, nexigen::logging::detail::format_param_t<Args...>,
             Args&&...) noexcept {}
    template <class... Args>
    void verbose(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void debug(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void info(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void warning(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void error(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void fatal(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    [[nodiscard]] core::Status flush() noexcept { return {}; }
    [[nodiscard]] core::Status quiesce() noexcept {
        ++quiesce_calls;
        if constexpr (Fail) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

private:
    core::LogLevel level_{core::LogLevel::Info};
};

using OkQuiesceLogger = CountingQuiesceLogger<false>;
using FailingQuiesceLogger = CountingQuiesceLogger<true>;

static_assert(nexigen::logging::concepts::Logger<OkQuiesceLogger>);
static_assert(QuiescableLogger<OkQuiesceLogger>);
static_assert(QuiescableLogger<FailingQuiesceLogger>);

/// A composite quiesces every member and stays idempotent.
void test_composite_quiesce() {
    CompositeLogger<NullLogger, NullLogger> logger{};
    NX_CHECK(logger.quiesce().has_value());
    NX_CHECK(logger.quiesce().has_value());
    NX_CHECK(logger.flush().has_value());
}

/// Every composite member is quiesced exactly once; the first failure wins and
/// does not short-circuit later members.
void test_composite_quiesce_members() {
    OkQuiesceLogger::quiesce_calls = 0;
    FailingQuiesceLogger::quiesce_calls = 0;
    CompositeLogger<OkQuiesceLogger, FailingQuiesceLogger> logger{};

    const core::Status status = logger.quiesce();
    NX_CHECK(!status.has_value());
    NX_CHECK(status.error() == ErrorCode::SystemError);
    NX_CHECK(OkQuiesceLogger::quiesce_calls == 1);
    NX_CHECK(FailingQuiesceLogger::quiesce_calls == 1);
}

/// Sink counting lifecycle calls (the worker constructs it in place).
template <class Record> struct CountingStopSink {
    static inline std::atomic<int> starts{0};
    static inline std::atomic<int> stops{0};
    static inline std::atomic<int> events{0};

    [[nodiscard]] core::Status start() noexcept {
        starts.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    core::Status on_event(const Record&) noexcept {
        events.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    [[nodiscard]] core::Status flush() noexcept { return {}; }
    void stop() noexcept { stops.fetch_add(1, std::memory_order_relaxed); }
    [[nodiscard]] std::string to_string() const { return "CountingStopSink"; }
};

/// The consumer stops the sink exactly once, however often `stop()` is called.
void test_worker_stop_once() {
    using Producer = nexigen::logging::detail::Producer<8, 96>;
    using Sink = CountingStopSink<typename Producer::record_type>;

    Sink::starts.store(0, std::memory_order_relaxed);
    Sink::stops.store(0, std::memory_order_relaxed);
    Sink::events.store(0, std::memory_order_relaxed);

    Producer producer{};
    NX_CHECK(producer.initialize().has_value());
    producer.set_level(LogLevel::Info);
    producer.emit(LogLevel::Info, "queued-before-stop");

    nexigen::logging::detail::AsyncWorker<Producer, Sink> worker{};
    NX_CHECK(worker.start(producer, nexigen::core::ThreadSpec{}).has_value());
    for (int i = 0; i < 5000 && !worker.writer_started(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NX_CHECK(worker.writer_started());
    NX_CHECK(Sink::starts.load() == 1);
    NX_CHECK(worker.running());

    worker.stop();
    NX_CHECK(!worker.running());
    NX_CHECK(Sink::stops.load() == 1);
    NX_CHECK(Sink::events.load() >= 1); // the queued record was drained

    worker.stop();
    NX_CHECK(Sink::stops.load() == 1); // never closed twice
}

} // namespace

void suite_logger_quiescence() {
    run_suite("logging.quiescence.null", test_null_quiesce);
    run_suite("logging.quiescence.console", test_console_quiesce);
    run_suite("logging.quiescence.file", test_file_quiesce);
    run_suite("logging.quiescence.composite", test_composite_quiesce);
    run_suite("logging.quiescence.composite_members", test_composite_quiesce_members);
    run_suite("logging.quiescence.worker_stop", test_worker_stop_once);
}
