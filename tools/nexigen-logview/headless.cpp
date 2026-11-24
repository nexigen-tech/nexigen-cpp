#include "headless.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <memory>
#include <ctime>
#include <thread>

#include <nexigen/logging/log_frame.hpp>

#include "backend/capture_thread.hpp"
#include "importer.hpp"
#include "time_formatter.hpp"

namespace nexigen::logview {

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) {
    g_stop.store(true, std::memory_order_relaxed);
}

nexigen::logging::LogFrame make_mock(std::uint64_t index) noexcept {
    nexigen::logging::LogFrame frame{};
    frame.timestamp_ns = 1'000'000'000ULL + index * 1'000'000ULL;
    const char* app = (index % 3U) == 0U ? "Bench" : ((index % 3U) == 1U ? "Engine" : "Viz");
    std::size_t app_size = 0;
    while (app[app_size] != '\0' && app_size < sizeof(frame.app_id)) {
        frame.app_id[app_size] = app[app_size];
        ++app_size;
    }
    if ((index % 50U) == 0U) {
        frame.log_level = nexigen::core::LogLevel::Warning;
    } else if ((index % 997U) == 0U) {
        frame.log_level = nexigen::core::LogLevel::Error;
    } else {
        frame.log_level = nexigen::core::LogLevel::Info;
    }
    frame.thread_id = 100U;
    frame.pid = 4242U;
    const int written = std::snprintf(reinterpret_cast<char*>(frame.payload), sizeof(frame.payload),
                                      "mock record %llu", static_cast<unsigned long long>(index));
    frame.payload_size = written > 0 ? static_cast<std::uint16_t>(written) : 0U;
    return frame;
}

void print_record(const RecordView& view, const TimeContext& time) {
    const FormattedTime stamp =
        TimeFormatter::format_timestamp(view.frame->timestamp_ns, TimestampMode::Absolute, time);
    if (view.kind == RecordKind::Frame) {
        std::printf("%llu frame %.47s %.*s %s %.*s\n", static_cast<unsigned long long>(view.seq),
                    stamp.text.data(), static_cast<int>(view.frame->app_id_text().size()),
                    view.frame->app_id_text().data(),
                    nexigen::core::to_string(view.frame->log_level).data(),
                    static_cast<int>(view.frame->payload_text().size()),
                    view.frame->payload_text().data());
    } else {
        std::printf("%llu %s %.47s gap=%llu\n", static_cast<unsigned long long>(view.seq),
                    nexigen::logview::to_string(view.kind).data(), stamp.text.data(),
                    static_cast<unsigned long long>(view.gap_count));
    }
}

} // namespace

std::uint64_t dump_records(LogViewModel& model, std::uint64_t limit) {
    std::uint64_t printed = 0;
    const RecordRing& ring = model.ring();
    for (std::uint64_t seq = ring.head(); seq < ring.tail(); ++seq) {
        if (limit != 0U && printed >= limit) {
            break;
        }
        if (const auto view = ring.find(seq); view.has_value()) {
            print_record(*view, model.time());
            ++printed;
        }
    }
    return printed;
}

/// @brief Import an offline file and dump the requested records.
///
/// @param model View model receiving the import.
/// @param options Parsed command line.
/// @return Process exit code.
int run_offline(LogViewModel& model, const CliOptions& options) {
    ImportStats stats{};
    const auto imported = model.import_now(options.offline_file, stats);
    if (!imported) {
        std::fprintf(stderr, "nexigen-logview: cannot open %s\n", options.offline_file.c_str());
        return 1;
    }
    while (model.filter().rebuilding()) {
        model.step_filter_rebuild(20'000U);
    }
    const std::uint64_t limit = options.max_records != 0U ? options.max_records : 0U;
    static_cast<void>(dump_records(model, limit));
    std::fprintf(stderr, "nexigen-logview: %s\n", stats.to_string().c_str());
    return 0;
}

/// @brief Emit one soak progress line when a whole minute has elapsed.
///
/// @param model View model supplying the rate, mode and visibility.
/// @param produced Frames produced so far.
/// @param next_report Next report threshold in seconds.
/// @param elapsed Elapsed whole seconds.
void report_soak(const LogViewModel& model, std::uint64_t produced, std::uint64_t& next_report,
                 std::int64_t elapsed) {
    if (static_cast<std::uint64_t>(elapsed) < next_report) {
        return;
    }
    std::fprintf(stderr, "soak: %llu records, rate=%.0f/s, mode=%s, visible=%llu\n",
                 static_cast<unsigned long long>(produced), model.rate(),
                 to_string(model.guard().mode()).data(),
                 static_cast<unsigned long long>(model.display_count()));
    next_report += 60U;
}

/// @brief Run the synthetic soak workload for the configured duration.
///
/// @param model View model receiving the synthetic frames.
/// @param options Parsed command line.
/// @return Process exit code.
int run_soak(LogViewModel& model, const CliOptions& options) {
    const auto begin = std::chrono::steady_clock::now();
    const auto duration = std::chrono::seconds(options.soak_seconds);
    const auto start_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(begin.time_since_epoch()).count();
    std::uint64_t produced = 0;
    std::uint64_t next_report = 60U;
    while (std::chrono::steady_clock::now() - begin < duration) {
        for (std::uint64_t i = 0; i < 500U; ++i) {
            model.ingest_frame(make_mock(produced), 0U);
            ++produced;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto now_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        timespec cpu_ts{};
        std::uint64_t cpu_ns = 0;
        if (::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_ts) == 0) {
            cpu_ns = static_cast<std::uint64_t>(cpu_ts.tv_sec) * 1'000'000'000ULL +
                     static_cast<std::uint64_t>(cpu_ts.tv_nsec);
        }
        static_cast<void>(model.tick(static_cast<std::uint64_t>(now_ns - start_ns), cpu_ns));
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - begin).count();
        report_soak(model, produced, next_report, elapsed);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::fprintf(stderr, "soak done: %llu records in %llu s, mode=%s\n",
                 static_cast<unsigned long long>(produced),
                 static_cast<unsigned long long>(options.soak_seconds),
                 to_string(model.guard().mode()).data());
    return 0;
}

/// @brief Ingest a fixed number of synthetic frames and dump them.
///
/// @param model View model receiving the synthetic frames.
/// @param options Parsed command line.
/// @return Process exit code.
int run_mock(LogViewModel& model, const CliOptions& options) {
    for (std::uint64_t i = 0; i < options.mock_records; ++i) {
        model.ingest_frame(make_mock(i), 0U);
    }
    const std::uint64_t limit =
        options.max_records != 0U ? options.max_records : options.mock_records;
    static_cast<void>(dump_records(model, limit));
    return 0;
}

/// @brief Drain the capture thread until stop or the record limit.
///
/// @param model View model receiving the drained frames.
/// @param options Parsed command line.
/// @return Process exit code.
int run_live(LogViewModel& model, const CliOptions& options) {
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);
    auto capture =
        std::make_unique<CaptureThread>(model.config().service_name, model.config().capture_thread);
    const auto started = capture->start();
    if (!started) {
        std::fprintf(stderr, "nexigen-logview: failed to start capture\n");
        return 1;
    }
    std::uint64_t total = 0;
    std::uint64_t last_overflow = 0;
    while (!g_stop.load(std::memory_order_relaxed)) {
        const CaptureStats stats = capture->stats();
        last_overflow = stats.overflow;
        const std::size_t drained = model.drain(capture->ingress(), last_overflow);
        total += drained;
        if (options.max_records != 0U && total >= options.max_records) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    capture->stop();
    static_cast<void>(dump_records(model, options.max_records));
    return 0;
}

int run_headless(LogViewModel& model, const CliOptions& options) {
    if (options.offline) {
        return run_offline(model, options);
    }
    if (options.soak_seconds > 0U) {
        return run_soak(model, options);
    }
    if (options.mock_records > 0U) {
        return run_mock(model, options);
    }
    return run_live(model, options);
}

} // namespace nexigen::logview
