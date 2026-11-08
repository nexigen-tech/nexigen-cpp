// Split from logging_test.cpp: nexigen::logging multi-thread stress.

#include "logging_test_support.hpp"

namespace {

void test_stress() {
    constexpr int k_threads = 6;
    constexpr int k_per_thread = 400;

    alignas(64) static char stress_base[512];
    std::snprintf(stress_base, sizeof stress_base, "/tmp/opencode/nexigen-stress-%d", ::getpid());

    // --- (a) FileLogger: verify exact per-thread content in the JSONL file.
    // Shard capacity 1024 > per-thread burst (400): under drained conditions
    // NOTHING drops (spec 7.1 acceptance), the flush barrier drains the pool.
    static FileLogger<1024, 128> fl{};
    {
        FileLoggerConfig cfg{};
        cfg.app_name = "stress";
        cfg.base_dir = stress_base;
        cfg.file_name = "stress.jsonl";
        cfg.initial_level = LogLevel::Verbose;
        cfg.rotation.kind = RotationSchedule::None;
        NX_CHECK(fl.initialize(cfg).has_value());
        for (int i = 0; i < 5000 && !fl.writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(fl.writer_started());
    }
    std::jthread workers[k_threads];
    for (int t = 0; t < k_threads; ++t) {
        workers[t] = std::jthread([t] { // fl is static: referenced, not captured
            for (int i = 0; i < k_per_thread; ++i) {
                fl.info("stress t{} #{}", t, i);
            }
        });
    }
    for (auto& w : workers) {
        w.join();
    }
    NX_CHECK(fl.flush().has_value());
    char fpath[560];
    std::snprintf(fpath, sizeof fpath, "%s/stress.jsonl", stress_base);
    alignas(64) static char fcontent[1 << 20];
    const int ffd = ::open(fpath, O_RDONLY);
    NX_CHECK(ffd >= 0);
    const std::size_t flen = ffd >= 0 ? read_all(ffd, fcontent, sizeof fcontent) : 0U;
    if (ffd >= 0) {
        ::close(ffd);
    }
    NX_CHECK(flen != 0U);
    const std::string_view fsv(fcontent, flen);
    bool all_present = true;
    for (int t = 0; t < k_threads; ++t) {
        for (int i = 0; i < k_per_thread; ++i) {
            char needle[48];
            // The trailing `"}` anchors at the message-field terminator so
            // "#1" cannot be matched inside "#10" (exactly-once verification).
            const int nn = std::snprintf(needle, sizeof needle, "stress t%d #%d\"}", t, i);
            if (fsv.find(std::string_view(needle, static_cast<std::size_t>(nn))) ==
                std::string_view::npos) {
                all_present = false;
            }
        }
    }
    NX_CHECK(all_present);
    NX_CHECK(file_line_count(fpath) == static_cast<std::size_t>(k_threads * k_per_thread));
    NX_CHECK(fl.dropped().total() == 0ULL); // flush drained every shard
    std::printf("stress: %d threads x %d records -> file sink, all present once, 0 dropped\n",
                k_threads, k_per_thread);

    // --- (b) ConsoleLogger: count distinct lines through a captured pipe.
    int out_pipe[2]{};
    NX_CHECK(::pipe(out_pipe) == 0);
    const int saved_out = ::dup(STDOUT_FILENO);
    NX_CHECK(::dup2(out_pipe[1], STDOUT_FILENO) == STDOUT_FILENO);
    {
        static ConsoleLogger<32, 128> cl{};
        ConsoleLogger<32, 128>::Config cfg{};
        cfg.initial_level = LogLevel::Verbose;
        NX_CHECK(cl.initialize(cfg).has_value());
        for (int i = 0; i < 5000 && !cl.writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::jthread cw[k_threads];
        for (int t = 0; t < k_threads; ++t) {
            cw[t] = std::jthread([t] { // cl is static: referenced, not captured
                for (int i = 0; i < 16; ++i) {
                    cl.info("cstress t{} #{}", t, i);
                }
            });
        }
        for (auto& w : cw) {
            w.join();
        }
        NX_CHECK(cl.flush().has_value());
    }
    NX_CHECK(::dup2(saved_out, STDOUT_FILENO) == STDOUT_FILENO);
    ::close(saved_out);
    ::close(out_pipe[1]);
    alignas(64) static char cbuf[1 << 18];
    const std::size_t clen = read_all(out_pipe[0], cbuf, sizeof cbuf);
    ::close(out_pipe[0]);
    NX_CHECK(count_char(cbuf, clen, '\n') == static_cast<std::size_t>(k_threads * 16));
    std::printf("stress: console sink delivered %d lines from %d producers\n", k_threads * 16,
                k_threads);

    // --- (c) Composite fan-out under concurrency: both leaves see everything.
    alignas(64) static char comp_stress[512];
    std::snprintf(comp_stress, sizeof comp_stress, "%.400s/comp", stress_base);
    static_cast<void>(::mkdir(comp_stress, 0755));
    {
        static CompositeLogger<NullLogger, FileLogger<64, 128>> comp{};
        FileLoggerConfig cfg{};
        cfg.app_name = "cstress";
        cfg.base_dir = comp_stress;
        cfg.file_name = "c.jsonl";
        cfg.initial_level = LogLevel::Verbose;
        cfg.rotation.kind = RotationSchedule::None;
        NX_CHECK(comp.get<1>().initialize(cfg).has_value());
        for (int i = 0; i < 5000 && !comp.get<1>().writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::jthread cw[k_threads];
        for (int t = 0; t < k_threads; ++t) {
            cw[t] = std::jthread([t] { // comp is static: referenced, not captured
                for (int i = 0; i < 50; ++i) {
                    comp.warning("fanout t{} #{}", t, i);
                }
            });
        }
        for (auto& w : cw) {
            w.join();
        }
        NX_CHECK(comp.flush().has_value());
        char cpath[560];
        std::snprintf(cpath, sizeof cpath, "%s/c.jsonl", comp_stress);
        NX_CHECK(file_line_count(cpath) == static_cast<std::size_t>(k_threads * 50));
        { // pack arguments survived the composite fold verbatim to the leaf
            alignas(64) static char cc[1 << 17];
            const int cfd = ::open(cpath, O_RDONLY);
            NX_CHECK(cfd >= 0);
            const std::size_t cn = cfd >= 0 ? read_all(cfd, cc, sizeof cc) : 0U;
            if (cfd >= 0) {
                ::close(cfd);
            }
            const std::string_view csv(cc, cn);
            NX_CHECK(csv.find("fanout t0 #0\"}") != std::string_view::npos);
            NX_CHECK(csv.find("fanout t5 #49\"}") != std::string_view::npos);
        }
        NX_CHECK(comp.get<1>().dropped().total() == 0ULL);
    }
    std::printf("stress: composite fan-out reached every leaf from %d producers\n", k_threads);
}

} // namespace

void suite_stress() {
    run_suite("stress", test_stress);
}
