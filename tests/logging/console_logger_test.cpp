// Split from console_logger_test.cpp: nexigen::logging ConsoleLogger.

#include "logging_test_support.hpp"

namespace {

// --- console logger suite (task 4.3 / spec logging/console-logger).

// Drain a pipe to EOF (write ends must be closed by caller first).

void test_console() {
    // Capture stdout/stderr via redirected fds: the console sink writes
    // straight to fd 1/2, so a process-wide dup2 routes the worker thread too.
    int out_pipe[2]{};
    int err_pipe[2]{};
    NX_CHECK(::pipe(out_pipe) == 0);
    NX_CHECK(::pipe(err_pipe) == 0);
    const int saved_out = ::dup(STDOUT_FILENO);
    const int saved_err = ::dup(STDERR_FILENO);
    NX_CHECK(saved_out >= 0 && saved_err >= 0);
    NX_CHECK(::dup2(out_pipe[1], STDOUT_FILENO) == STDOUT_FILENO);
    NX_CHECK(::dup2(err_pipe[1], STDERR_FILENO) == STDERR_FILENO);

    int storm_accepted = 0;
    int storm_full = 0;
    {
        ConsoleLogger<32, 128> logger{};
        ConsoleLogger<32, 128>::Config cfg{};
        cfg.initial_level = LogLevel::Verbose;
        NX_CHECK(logger.initialize(cfg).has_value());
        for (int i = 0; i < 1000 && !logger.writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(logger.writer_started());

        // Routing + content: debug/info -> stdout, warning/fatal -> stderr.
        logger.debug("stdout dbg");
        logger.info("hello out");
        logger.warning("hello err");
        logger.fatal("fatal err");
        // Deferred render (spec console-logger scenarios): float pack and enum.
        logger.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025);
        logger.info("Level is {}", LogLevel::Warning);
        NX_CHECK(logger.flush().has_value());

        // Fill the tiny shard: calls return immediately, drops are counted
        // (the producer never waits on console backpressure).
        const std::uint64_t drops_before = logger.dropped().buffer_full;
        for (int i = 0; i < 64; ++i) {
            logger.info("storm");
        }
        storm_full = static_cast<int>(logger.dropped().buffer_full - drops_before);
        storm_accepted = 64 - storm_full;
        NX_CHECK(storm_accepted + storm_full == 64);
        NX_CHECK(logger.flush().has_value());

        // Logger dump (mandate 9): level + pool shape + counters.
        const std::string dump = logger.to_string();
        NX_CHECK(dump.find("ConsoleLogger(level=verbose") != std::string::npos);
        NX_CHECK(dump.find("shard_capacity=32") != std::string::npos);
        NX_CHECK(dump.find("shards=32") != std::string::npos);

        // Destruction without flush still drains everything accepted.
        {
            ConsoleLogger<16, 128> transient{};
            ConsoleLogger<16, 128>::Config tcfg{};
            tcfg.initial_level = LogLevel::Verbose;
            NX_CHECK(transient.initialize(tcfg).has_value());
            transient.info("destr a");
            transient.info("destr b");
        } // joins here; both lines must be in the pipe
    }

    // Restore the real terminals and close our write ends so readers hit EOF.
    NX_CHECK(::dup2(saved_out, STDOUT_FILENO) == STDOUT_FILENO);
    NX_CHECK(::dup2(saved_err, STDERR_FILENO) == STDERR_FILENO);
    ::close(saved_out);
    ::close(saved_err);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    alignas(64) static char out_buf[1 << 17];
    alignas(64) static char err_buf[1 << 17];
    const std::size_t out_len = read_all(out_pipe[0], out_buf, sizeof out_buf);
    const std::size_t err_len = read_all(err_pipe[0], err_buf, sizeof err_buf);
    ::close(out_pipe[0]);
    ::close(err_pipe[0]);

    // Exactly one line per accepted record (incl. the destructor-drained pair),
    // in FIFO order, with self-descriptive content. The drop summary from
    // flush() may add one extra stderr line (Warning level).
    const std::size_t expected_out = 6 + static_cast<std::size_t>(storm_accepted);
    NX_CHECK(count_char(out_buf, out_len, '\n') == expected_out);
    NX_CHECK(count_char(err_buf, err_len, '\n') >= 2); // 2 user + optional drop summary
    NX_CHECK(std::strstr(out_buf, "stdout dbg") != nullptr);
    NX_CHECK(std::strstr(out_buf, "hello out") != nullptr);
    NX_CHECK(std::strstr(out_buf, "hello err") == nullptr);
    NX_CHECK(std::strstr(out_buf, "destr a") != nullptr);
    NX_CHECK(std::strstr(out_buf, "destr b") != nullptr);
    // rendered deferred formats reached the console exactly (spec scenarios)
    NX_CHECK(std::strstr(out_buf, "Прямой вызов метода debug. Давление: 0.0025") != nullptr);
    NX_CHECK(std::strstr(out_buf, "Level is warning") != nullptr);
    NX_CHECK(std::strstr(err_buf, "hello err") != nullptr);
    NX_CHECK(std::strstr(err_buf, "fatal err") != nullptr);
    NX_CHECK(std::strstr(err_buf, "storm") == nullptr);
    // ordering on the stdout stream
    const char* p_dbg = std::strstr(out_buf, "stdout dbg");
    const char* p_hi = std::strstr(out_buf, "hello out");
    NX_CHECK(p_dbg != nullptr && p_hi != nullptr && p_dbg < p_hi);
    // self-descriptive pieces: lowercase level, basename:line, function
    NX_CHECK(std::strstr(out_buf, " info ") != nullptr);
    NX_CHECK(std::strstr(out_buf, "console_logger_test.cpp:") != nullptr);
    NX_CHECK(std::strstr(out_buf, "test_console") != nullptr);
    // leading monotonic seconds.nanoseconds stamp
    NX_CHECK(out_buf[0] >= '0' && out_buf[0] <= '9');
    NX_CHECK(std::strchr(out_buf, '.') != nullptr);
    std::printf("console: out=%zu B (%zu lines), err=%zu B, storm ok=%d full=%d\n", out_len,
                expected_out, err_len, storm_accepted, storm_full);
}

// --- JSON formatter suite (task 5.4 / spec logging/file-logger schema).

} // namespace

void suite_console() {
    run_suite("console", test_console);
}
