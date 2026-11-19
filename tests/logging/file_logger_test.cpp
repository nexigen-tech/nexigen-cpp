// Split from file_logger_test.cpp: nexigen::logging FileLogger.

#include "logging_test_support.hpp"
#include "../support/test_time_base.hpp"
#include <nexigen/chrono/civil_time.hpp>

namespace {

/// Per-run base directory under the shared scratch root; the logger itself
/// creates it (open_period), the parent /tmp/opencode pre-exists.
const char* file_logger_base() {
    alignas(64) static char base[512];
    static const bool initialized = [] {
        std::snprintf(base, sizeof base, "/tmp/opencode/nexigen-fl-%d", ::getpid());
        return true;
    }();
    (void)initialized;
    return base;
}

template <class Logger> void wait_writer(Logger& lg) {
    for (int i = 0; i < 5000 && !lg.writer_started(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NX_CHECK(lg.writer_started());
}

/// A rejected configuration: InvalidArgument and no worker launched.
void expect_invalid(Status status) {
    NX_CHECK(!status.has_value());
    if (!status.has_value()) {
        NX_CHECK(status.error() == ErrorCode::InvalidArgument);
    }
}

/// (a) config validation rejects bad setups WITHOUT starting the worker.
void test_file_logger_validation() {
    const char* flbase = file_logger_base();

    static const char long_name[] =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; // 70
                                                                                            // a's
    static FileLogger<64, 128> flv{};
    {
        FileLoggerConfig bad{};
        bad.base_dir = ""; // required
        expect_invalid(flv.initialize(bad));
        bad.base_dir = flbase;
        bad.rotation = RotationConfig{RotationSchedule::Custom, 0ULL}; // zero period
        expect_invalid(flv.initialize(bad));
        bad.rotation = RotationConfig{RotationSchedule::Hourly, 0ULL};
        bad.throttle = ThrottleConfig{true, 5ULL, 0ULL}; // zero window
        expect_invalid(flv.initialize(bad));
        bad.throttle = {};
        bad.file_name = std::string_view(long_name, sizeof long_name - 1); // > 63
        expect_invalid(flv.initialize(bad));
        NX_CHECK(!flv.writer_started()); // nothing was launched by rejected configs
    }
}

/// (b) happy path, Rotation::None: one fixed file, structural schema.
void test_file_logger_happy_path() {
    const char* flbase = file_logger_base();

    // --- (b) happy path, Rotation::None: one fixed file, structural schema.
    static FileLogger<256, 128> flh{};
    {
        FileLoggerConfig cfg{};
        cfg.app_name = "unit";
        cfg.base_dir = flbase;
        cfg.file_name = "app.jsonl";
        cfg.rotation.kind = RotationSchedule::None;
        NX_CHECK(flh.initialize(cfg).has_value());
        wait_writer(flh);
        NX_CHECK(flh.writer_start_code() == ErrorCode::Ok);

        flh.info("file hello");
        flh.warning("second line");
        flh.info("rendered {} {}", 3.5, LogLevel::Info);
        NX_CHECK(flh.flush().has_value());

        char path[560];
        std::snprintf(path, sizeof path, "%s/app.jsonl", flbase);
        char content[40096];
        const std::size_t n = read_whole_file(path, content);
        NX_CHECK(n != SIZE_MAX);
        const std::string_view sv(content, n);
        NX_CHECK(sv.substr(0, 14) == "{\"timestamp\":\"");
        NX_CHECK(sv.find("\"mono_ns\":") != std::string_view::npos);
        NX_CHECK(sv.find("\"boot\":\"") != std::string_view::npos);
        if (sv.size() > 64) { // structural deep-checks only with real content present
            // exact field order: timestamp < level < app < thread_id < message
            const std::size_t i_ts = sv.find("\"timestamp\"");
            const std::size_t i_lv = sv.find("\"level\":\"info\"");
            const std::size_t i_ap = sv.find("\"app\":\"unit\"");
            const std::size_t i_ti = sv.find("\"thread_id\":");
            const std::size_t i_me = sv.find("\"message\":\"[file_logger_test.cpp:");
            NX_CHECK(i_ts < i_lv && i_lv < i_ap && i_ap < i_ti && i_ti < i_me);
            // 30-char ISO-8601: "YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ"
            const std::size_t q = sv.find('\"', i_ts + 12);
            NX_CHECK(q != std::string_view::npos && q + 31 < sv.size());
            if (q != std::string_view::npos && q + 31 < sv.size()) {
                const std::size_t ts_begin = q + 1;
                NX_CHECK(sv[ts_begin + 10] == 'T');
                NX_CHECK(sv[ts_begin + 19] == '.');
                NX_CHECK(sv[ts_begin + 29] == 'Z');
                NX_CHECK(sv[ts_begin + 30] == '\"');
            }
        }
        // message: "[basename:line] (function) text"}
        NX_CHECK(sv.find("file hello\"}\n") != std::string_view::npos);
        NX_CHECK(sv.find("test_file_logger") != std::string_view::npos);
        NX_CHECK(sv.find("second line") != std::string_view::npos);
        NX_CHECK(sv.find("rendered 3.5 info") != std::string_view::npos); // deferred render (spec)
        NX_CHECK(file_line_count(path) == 3);
        const std::string fl_dump = flh.to_string();
        NX_CHECK(fl_dump.find("FileLogger(level=info") != std::string::npos);
        NX_CHECK(fl_dump.find("shard_capacity=256") != std::string::npos);
        NX_CHECK(fl_dump.find("dropped=") != std::string::npos);
        NX_CHECK(to_string(flh) == fl_dump);
    }
}

/// (c) flush surfaces a real write failure: /dev/full answers ENOSPC.
void test_file_logger_write_failure() {
    // --- (c) flush surfaces a real write failure: /dev/full answers ENOSPC.
    static FileLogger<64, 128> fld{};
    {
        FileLoggerConfig cfg{};
        cfg.app_name = "dev";
        cfg.base_dir = "/dev";  // mkdirat -> EEXIST (ignored)
        cfg.file_name = "full"; // character device that fails every write
        cfg.rotation.kind = RotationSchedule::None;
        NX_CHECK(fld.initialize(cfg).has_value());
        wait_writer(fld);
        fld.info("into the void"); // accepted wait-free regardless of the eventual write failure
        const Status f = fld.flush();
        NX_CHECK(!f.has_value());
        NX_CHECK(f.error() == ErrorCode::SystemError); // spec: "Flush surfaces write failure"
    }
}

/// (d) throttling: budget + RateLimited + automatic drop summary (6.2).
void test_file_logger_throttling() {
    const char* flbase = file_logger_base();

    // --- (d) throttling: budget + RateLimited + automatic drop summary (6.2).
    static FileLogger<256, 128> flt{};
    {
        alignas(64) static char thr_base[512];
        std::snprintf(thr_base, sizeof thr_base, "%.500s/thr", flbase);
        FileLoggerConfig cfg{};
        cfg.app_name = "thr";
        cfg.base_dir = thr_base;
        cfg.file_name = "thr.jsonl";
        cfg.rotation.kind = RotationSchedule::None;
        cfg.throttle = ThrottleConfig{true, 3ULL, 200'000'000ULL}; // 3 per 200 ms
        NX_CHECK(flt.initialize(cfg).has_value());
        wait_writer(flt);

        const DroppedStats thr_before = flt.dropped();
        for (int i = 0; i < 10; ++i) {
            flt.info("thr user");
        }
        const DroppedStats thr_after = flt.dropped();
        const std::uint64_t rl = thr_after.rate_limited - thr_before.rate_limited;
        const int ok = 10 - static_cast<int>(rl);
        NX_CHECK(ok == 3);
        NX_CHECK(rl == 7);
        NX_CHECK(thr_after.buffer_full == 0ULL);

        // Resume in the next window: exactly one user record, and the summary
        // must land in the file reporting the exact per-cause counts.
        std::this_thread::sleep_for(std::chrono::milliseconds(220));
        flt.info("thr resumed");
        NX_CHECK(flt.flush().has_value());
        NX_CHECK(flt.dropped().total() == 0ULL); // counters cleared by the summary

        char path[560];
        std::snprintf(path, sizeof path, "%s/thr.jsonl", thr_base);
        char content[40096];
        const std::size_t n = read_whole_file(path, content);
        NX_CHECK(n != SIZE_MAX);
        const std::string_view sv(content, n);
        NX_CHECK(file_line_count(path) == 5); // 3 + summary + 1
        const std::size_t p_user3 =
            sv.find("thr user\"}", sv.find("thr user", sv.find("thr user") + 1) + 1);
        const std::size_t p_sum = sv.find("Dropped 7 record(s) in the last window (rate-limited: "
                                          "7, buffer-full: 0, slot-exhausted: 0, throttled: 0).");
        const std::size_t p_res = sv.find("thr resumed");
        NX_CHECK(p_user3 != std::string_view::npos);
        NX_CHECK(p_sum != std::string_view::npos);
        NX_CHECK(p_res != std::string_view::npos);
        if (p_user3 != std::string_view::npos && p_sum != std::string_view::npos &&
            p_res != std::string_view::npos) {
            NX_CHECK(p_user3 < p_sum && p_sum < p_res);
        }
        // the summary itself is a warning record
        const std::size_t sum_line_begin = sv.rfind('\n', p_sum) + 1;
        NX_CHECK(sv.substr(sum_line_begin, p_sum - sum_line_begin).find("\"level\":\"warning\"") !=
                 std::string_view::npos);
        std::printf("file_logger: throttle ok=%d rl=%llu summary line verified\n", ok,
                    static_cast<unsigned long long>(rl));
    }
}

/// (e) rotation + affinity during a live stream (6.3).
void test_file_logger_rotation() {
    const char* flbase = file_logger_base();

    // --- (e) rotation + affinity during a live stream (6.3).
    static FileLogger<256, 128> flr{};
    {
        std::uint64_t proc_mask = 0;
        static_cast<void>(nexigen::logging::detail::self_affinity(proc_mask));
        int allowed = 0;
        for (std::uint64_t m = proc_mask; m != 0ULL; m &= m - 1ULL) {
            ++allowed;
        }
        const std::uint64_t target = (allowed >= 2) ? (proc_mask & (~proc_mask + 1ULL)) : 0ULL;

        alignas(64) static char rot_base[512];
        std::snprintf(rot_base, sizeof rot_base, "%.500s/rot", flbase);
        FileLoggerConfig cfg{};
        cfg.app_name = "rot";
        cfg.base_dir = rot_base;
        cfg.file_name = "app.jsonl";
        cfg.rotation.kind = RotationSchedule::Custom;
        cfg.rotation.custom_ns = 150'000'000ULL; // 150 ms windows
        cfg.worker_thread.set_cpus(core::CpuSet::from_core_mask(target));
        NX_CHECK(flr.initialize(cfg).has_value());
        wait_writer(flr);
        NX_CHECK(flr.writer_start_code() == ErrorCode::Ok);
        if (target != 0ULL) {
            // spec: pinned writer reports the configured mask
            NX_CHECK(flr.writer_mask() == target);
            NX_CHECK((flr.writer_thread_report().applied() &
                      core::ThreadApplyReport::affinity_mask) != 0U);
            NX_CHECK(flr.writer_thread_report().failed() == 0U);
        } else {
            std::printf("rotation test: single permitted core, mask assertion skipped\n");
        }

        // ~600 ms of 10 ms-paced records spans >= 3 period windows.
        std::uint64_t accepted = 0;
        for (int i = 0; i < 60; ++i) {
            const std::uint64_t drops_b = flr.dropped().total();
            flr.info("rot stream");
            if (flr.dropped().total() == drops_b) {
                ++accepted;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        NX_CHECK(flr.flush().has_value());
        NX_CHECK(accepted == 60); // 256-shard with a 10 ms cadence drains easily

        int period_dirs = 0;
        std::uint64_t lines_total = 0;
        DIR* d = ::opendir(rot_base);
        NX_CHECK(d != nullptr);
        if (d != nullptr) {
            while (const dirent* e = ::readdir(d)) {
                const std::string_view name{e->d_name};
                if (name.rfind("epoch-", 0) == 0) {
                    ++period_dirs;
                    char path[560];
                    std::snprintf(path, sizeof path, "%s/%.*s/app.jsonl", rot_base,
                                  static_cast<int>(name.size()), name.data());
                    const std::size_t lc = file_line_count(path);
                    NX_CHECK(lc != SIZE_MAX);
                    if (lc != SIZE_MAX) {
                        lines_total += lc;
                    }
                }
            }
            ::closedir(d);
        }
        NX_CHECK(period_dirs >= 3);        // >= 600 ms / 150 ms
        NX_CHECK(lines_total == accepted); // nothing lost or duplicated at the switch
        std::printf("file_logger: rotation across %d period dirs, %llu lines == %llu accepted\n",
                    period_dirs, static_cast<unsigned long long>(lines_total),
                    static_cast<unsigned long long>(accepted));
    }
}

/// (f) soak (6.4): producers flat-out against a formatting consumer —
/// wait-free drops, zero allocation in BOTH threads across the window.
void test_file_logger_soak() {
    const char* flbase = file_logger_base();

    // --- (f) soak (6.4): producers flat-out against a formatting consumer —
    // wait-free drops, zero allocation in BOTH threads across the window.
    static FileLogger<1024, 128> fls{};
    {
        alignas(64) static char soak_base[512];
        std::snprintf(soak_base, sizeof soak_base, "%.500s/soak", flbase);
        FileLoggerConfig cfg{};
        cfg.app_name = "soak";
        cfg.base_dir = soak_base;
        cfg.file_name = "soak.jsonl";
        cfg.rotation.kind = RotationSchedule::None; // clean accounting (no summaries)
        NX_CHECK(fls.initialize(cfg).has_value());
        wait_writer(fls);

        const std::size_t arena_b = g_alloc_used;
        const std::size_t heap_b = heap_in_use();

        const chrono::Timestamp t_start = Timestamp::now().value_or(Timestamp{0});
        std::uint64_t attempts = 0;
        for (;;) {
            fls.info("soak payload line");
            ++attempts; // every call completed immediately, accepted or dropped-counted
            const core::Result<Timestamp> t_now = Timestamp::now();
            if (!t_now || (t_now->nanoseconds() - t_start.nanoseconds()) > 120'000'000ULL) {
                break; // ~120 ms window
            }
        }
        const std::uint64_t accepted = attempts - fls.dropped().buffer_full;
        // Both threads were active the entire window; nothing allocated.
        NX_CHECK(g_alloc_used == arena_b);
        NX_CHECK(heap_in_use() == heap_b);
        NX_CHECK(fls.dropped().buffer_full == attempts - accepted);

        NX_CHECK(fls.flush().has_value());
        char path[560];
        std::snprintf(path, sizeof path, "%s/soak.jsonl", soak_base);
        // flush() pushes a drop summary record; line count = accepted + 1 summary.
        NX_CHECK(file_line_count(path) == static_cast<std::size_t>(accepted) + 1U);
        std::printf("file_logger: soak attempts=%llu accepted=%llu dropped=%llu lines=%llu\n",
                    static_cast<unsigned long long>(attempts),
                    static_cast<unsigned long long>(accepted),
                    static_cast<unsigned long long>(fls.dropped().buffer_full),
                    static_cast<unsigned long long>(accepted));
    }
}

void test_file_logger_time_base_source() {
    // Custom +1h calibration: the sink must use the published offset instead
    // of sampling the wall clock, and keep mono_ns authoritative.
    chrono::TimeBaseClock::detach();
    const std::string offset_name =
        nexigen::tests::publish_test_time_base("offset", 3'600'000'000'000LL, /*calibrated=*/true);
    NX_CHECK(!offset_name.empty());
    {
        static FileLogger<64, 128> logger{};
        FileLoggerConfig cfg{};
        cfg.app_name = "tb-offset";
        cfg.base_dir = std::string(file_logger_base()) + "-tb-offset";
        cfg.file_name = "app.jsonl";
        cfg.rotation.kind = RotationSchedule::None;
        NX_CHECK(logger.initialize(cfg).has_value());
        wait_writer(logger);
        logger.info("first");
        logger.info("second");
        logger.info("third");
        NX_CHECK(logger.flush().has_value());

        std::ifstream input{cfg.base_dir + "/app.jsonl"};
        NX_CHECK(input.is_open());
        std::string line;
        std::uint64_t previous_mono = 0;
        std::string previous_timestamp;
        std::size_t lines = 0;
        while (std::getline(input, line)) {
            const nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
            NX_CHECK(!parsed.is_discarded());
            if (parsed.is_discarded()) {
                continue;
            }
            const std::string timestamp = parsed.at("timestamp").get<std::string>();
            const std::uint64_t mono = parsed.at("mono_ns").get<std::uint64_t>();
            char expected[31];
            chrono::format_utc_iso8601_ns(static_cast<std::int64_t>(mono) + 3'600'000'000'000LL,
                                          expected);
            NX_CHECK(timestamp == std::string_view(expected, 30U));
            NX_CHECK(mono >= previous_mono);
            NX_CHECK(timestamp >= previous_timestamp);
            previous_mono = mono;
            previous_timestamp = timestamp;
            ++lines;
        }
        NX_CHECK(lines == 3U);
    }

    // Without a covering calibration the line carries mono_ns/boot and a null
    // timestamp, never a fabricated UTC value.
    chrono::TimeBaseClock::detach();
    const std::string empty_name =
        nexigen::tests::publish_test_time_base("uncalibrated", 0, /*calibrated=*/false);
    NX_CHECK(!empty_name.empty());
    {
        static FileLogger<64, 128> logger{};
        FileLoggerConfig cfg{};
        cfg.app_name = "tb-uncalibrated";
        cfg.base_dir = std::string(file_logger_base()) + "-tb-uncal";
        cfg.file_name = "app.jsonl";
        cfg.rotation.kind = RotationSchedule::None;
        NX_CHECK(logger.initialize(cfg).has_value());
        wait_writer(logger);
        logger.info("uncalibrated");
        NX_CHECK(logger.flush().has_value());

        std::ifstream input{cfg.base_dir + "/app.jsonl"};
        NX_CHECK(input.is_open());
        std::string line;
        NX_CHECK(static_cast<bool>(std::getline(input, line)));
        NX_CHECK(line.find("\"timestamp\":null") != std::string::npos);
        NX_CHECK(line.find("\"mono_ns\":") != std::string::npos);
        NX_CHECK(line.find("\"boot\":\"") != std::string::npos);
    }

    chrono::TimeBaseClock::detach();
    nexigen::tests::install_test_time_base();

    // With the calibrated default base, file timestamps track wall time.
    {
        static FileLogger<64, 128> logger{};
        FileLoggerConfig cfg{};
        cfg.app_name = "tb-wall";
        cfg.base_dir = std::string(file_logger_base()) + "-tb-wall";
        cfg.file_name = "app.jsonl";
        cfg.rotation.kind = RotationSchedule::None;
        NX_CHECK(logger.initialize(cfg).has_value());
        wait_writer(logger);
        logger.info("wall");
        NX_CHECK(logger.flush().has_value());

        std::ifstream input{cfg.base_dir + "/app.jsonl"};
        std::string line;
        NX_CHECK(static_cast<bool>(std::getline(input, line)));
        const nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
        NX_CHECK(!parsed.is_discarded());
        if (parsed.is_object()) {
            ::timespec realtime{};
            NX_CHECK(::clock_gettime(CLOCK_REALTIME, &realtime) == 0);
            const std::int64_t now_utc =
                static_cast<std::int64_t>(realtime.tv_sec) * 1'000'000'000LL + realtime.tv_nsec;
            char expected[31];
            char previous[31];
            chrono::format_utc_iso8601_ns(now_utc, expected);
            chrono::format_utc_iso8601_ns(now_utc - 1'000'000'000LL, previous);
            const std::string timestamp = parsed.at("timestamp").get<std::string>();
            // Same second, or the immediately neighbouring one when the log
            // crossed a second boundary.
            NX_CHECK(timestamp.substr(0, 19) == std::string_view(expected, 19U) ||
                     timestamp.substr(0, 19) == std::string_view(previous, 19U));
        }
    }
}

/// FileLogger cold path: validation, schema, write failure, throttling,
/// rotation/affinity and the allocation-free soak (spec logging/file-logger).
void test_file_logger() {
    test_file_logger_validation();
    test_file_logger_happy_path();
    test_file_logger_write_failure();
    test_file_logger_throttling();
    test_file_logger_rotation();
    test_file_logger_soak();
    test_file_logger_time_base_source();
}
} // namespace

void suite_file_logger() {
    run_suite("file_logger", test_file_logger);
}
