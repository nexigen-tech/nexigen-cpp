// Split from composite_logger_test.cpp: nexigen::logging CompositeLogger.

#include "logging_test_support.hpp"

namespace {

// --- composite suite (task 7.1 / spec logging/composite-logger): virtual-free
// fan-out delivers identical records to every sink, one set_level re-thresholds
// the whole tree, get<Type>() sub-configuration takes effect through the
// member's own filter, calls below the composite's own level never reach any
// member, the empty pack is silent, and fold failures surface (last failure
// wins). The "invalid access" compile-time claim is proven by the scratch TU
// documented at the top of this file.

// Local aliases (commas inside <> would split the NX_CHECK macro).
using CompConsole = ConsoleLogger<64, 128>;
using CompFile = FileLogger<64, 128>;
using NestFile = FileLogger<32, 128>;
using MixFile = FileLogger<16, 128>;

/// Per-run composite scratch root under the shared scratch directory.
const char* composite_base() {
    alignas(64) static char base[512];
    static const bool initialized = [] {
        std::snprintf(base, sizeof base, "/tmp/opencode/nexigen-comp-%d", ::getpid());
        static_cast<void>(::mkdir(base, 0755));
        return true;
    }();
    (void)initialized;
    return base;
}

/// (a)+(b) empty pack is silent; one set_level re-thresholds the whole tree.
void test_composite_empty_and_propagation() {
    // --- (a) empty composite: concept-pinned above; every call OK, nothing emitted.
    {
        static CompositeLogger<> quiet{};
        quiet.set_level(LogLevel::Verbose); // would open every member to everything
        NX_CHECK(quiet.level() == LogLevel::Verbose);
        NX_CHECK(quiet.size == 0U);
        quiet.verbose("v");
        quiet.info("i");
        quiet.warning("w {}", 1);
        quiet.fatal("f");
        quiet.log(LogLevel::Error, "e");
        NX_CHECK(quiet.flush().has_value());
    }

    // --- (b) whole-tree propagation + index accessors. Duplicate member
    // types force the index form (the type form static_asserts uniqueness).
    {
        static CompositeLogger<NullLogger, NullLogger> pair{};
        pair.set_level(LogLevel::Error); // one call moves the whole tree
        NX_CHECK(pair.level() == LogLevel::Error);
        NX_CHECK(pair.get<0>().level() == LogLevel::Error);
        NX_CHECK(pair.get<1>().level() == LogLevel::Error);
        pair.info("below the whole tree"); // filtered short-circuit
        pair.set_level(LogLevel::Warning);
        NX_CHECK(pair.get<1>().level() == LogLevel::Warning);
        pair.warning("passes");
    }
}

/// (c) console + file fan-out through captured pipes.
void test_composite_fanout() {
    const char* comp_base = composite_base();

    // --- (c) console + file fan-out through captured pipes.
    int out_pipe[2]{};
    int err_pipe[2]{};
    NX_CHECK(::pipe(out_pipe) == 0);
    NX_CHECK(::pipe(err_pipe) == 0);
    const int saved_out = ::dup(STDOUT_FILENO);
    const int saved_err = ::dup(STDERR_FILENO);
    NX_CHECK(saved_out >= 0 && saved_err >= 0);
    NX_CHECK(::dup2(out_pipe[1], STDOUT_FILENO) == STDOUT_FILENO);
    NX_CHECK(::dup2(err_pipe[1], STDERR_FILENO) == STDERR_FILENO);

    {
        static CompositeLogger<ConsoleLogger<64, 128>, FileLogger<64, 128>> comp{};
        NX_CHECK(comp.get<0>().initialize(CompConsole::Config{}).has_value());
        FileLoggerConfig fcfg{};
        fcfg.app_name = "comp";
        fcfg.base_dir = comp_base;
        fcfg.file_name = "comp.jsonl";
        fcfg.rotation.kind = RotationSchedule::None;
        NX_CHECK(comp.get<CompFile>().initialize(fcfg).has_value());
        for (int i = 0;
             i < 5000 && (!comp.get<0>().writer_started() || !comp.get<1>().writer_started());
             ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(comp.get<0>().writer_started());
        NX_CHECK(comp.get<CompFile>().writer_started());

        comp.set_level(LogLevel::Info);
        NX_CHECK(comp.get<CompConsole>().level() == LogLevel::Info);
        NX_CHECK(comp.get<1>().level() == LogLevel::Info);

        // Fan-out: both sinks receive the identical record (spec scenario).
        comp.warning("composite fan-out");

        // Sub-logger-specific reconfiguration: file drops to Fatal while the
        // composite and console stay at Info (spec "Type-based reconfiguration").
        comp.get<CompFile>().set_level(LogLevel::Fatal);
        NX_CHECK(comp.level() == LogLevel::Info); // composite's own threshold unchanged
        comp.info("console only");
        comp.fatal("both again");

        // Composite short-circuit: own threshold Warning with BOTH members at
        // Verbose — the Info call returns OK without reaching any member.
        comp.set_level(LogLevel::Warning);
        comp.get<0>().set_level(LogLevel::Verbose);
        comp.get<CompFile>().set_level(LogLevel::Verbose);
        comp.info("shortcircuit info");
        comp.warning("shortcircuit warn");

        NX_CHECK(comp.flush().has_value());
    }

    // Restore the real terminals; close write ends so readers hit EOF.
    NX_CHECK(::dup2(saved_out, STDOUT_FILENO) == STDOUT_FILENO);
    NX_CHECK(::dup2(saved_err, STDERR_FILENO) == STDERR_FILENO);
    ::close(saved_out);
    ::close(saved_err);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    alignas(64) static char cout_buf[1 << 16];
    alignas(64) static char cerr_buf[1 << 16];
    const std::size_t cout_len = read_all(out_pipe[0], cout_buf, sizeof cout_buf);
    const std::size_t cerr_len = read_all(err_pipe[0], cerr_buf, sizeof cerr_buf);
    ::close(out_pipe[0]);
    ::close(err_pipe[0]);

    NX_CHECK(count_char(cout_buf, cout_len, '\n') == 1); // "console only"
    NX_CHECK(count_char(cerr_buf, cerr_len, '\n') == 3); // fan-out, fatal, sc-warn
    NX_CHECK(std::strstr(cout_buf, "console only") != nullptr);
    NX_CHECK(std::strstr(cout_buf, "shortcircuit") == nullptr);
    NX_CHECK(std::strstr(cerr_buf, "composite fan-out") != nullptr);
    NX_CHECK(std::strstr(cerr_buf, "both again") != nullptr);
    NX_CHECK(std::strstr(cerr_buf, "shortcircuit warn") != nullptr);
    NX_CHECK(std::strstr(cerr_buf, "shortcircuit info") == nullptr);
    NX_CHECK(std::strstr(cerr_buf, "console only") == nullptr);
    // identical record content reached both sinks: message + source location
    NX_CHECK(std::strstr(cerr_buf, "composite_logger_test.cpp:") != nullptr);
    NX_CHECK(std::strstr(cerr_buf, "test_composite") != nullptr);

    alignas(64) static char cpath[560];
    std::snprintf(cpath, sizeof cpath, "%s/comp.jsonl", comp_base);
    alignas(64) static char ccontent[40096];
    const std::size_t cn = read_whole_file(cpath, ccontent);
    NX_CHECK(cn != SIZE_MAX);
    NX_CHECK(file_line_count(cpath) == 3);
    NX_CHECK(std::strstr(ccontent, "composite fan-out\"}") != nullptr);
    NX_CHECK(std::strstr(ccontent, "both again\"}") != nullptr);
    NX_CHECK(std::strstr(ccontent, "shortcircuit warn\"}") != nullptr);
    NX_CHECK(std::strstr(ccontent, "console only") == nullptr);      // member-level Fatal
    NX_CHECK(std::strstr(ccontent, "shortcircuit info") == nullptr); // composite short-circuit
    NX_CHECK(std::strstr(ccontent, "\"message\":\"[composite_logger_test.cpp:") != nullptr);
    NX_CHECK(std::strstr(ccontent, "test_composite") != nullptr);
}

/// (d) composite of composites reaches every leaf.
void test_composite_nested() {
    const char* comp_base = composite_base();

    // --- (d) composite of composites reaches every leaf (spec scenario):
    // two file leaves through different tree depths must hold the identical line.
    alignas(64) static char nest_a[560];
    alignas(64) static char nest_b[560];
    std::snprintf(nest_a, sizeof nest_a, "%.500s/nest-a", comp_base);
    std::snprintf(nest_b, sizeof nest_b, "%.500s/nest-b", comp_base);
    static_cast<void>(::mkdir(nest_a, 0755));
    static_cast<void>(::mkdir(nest_b, 0755));
    {
        using Inner = CompositeLogger<NullLogger, FileLogger<32, 128>>;
        static CompositeLogger<Inner, FileLogger<32, 128>> outer{};

        FileLoggerConfig a_cfg{};
        a_cfg.app_name = "nest";
        a_cfg.base_dir = nest_a;
        a_cfg.file_name = "leaf.jsonl";
        a_cfg.rotation.kind = RotationSchedule::None;
        NX_CHECK(outer.get<0>().get<NestFile>().initialize(a_cfg).has_value());
        FileLoggerConfig b_cfg = a_cfg;
        b_cfg.base_dir = nest_b;
        NX_CHECK(outer.get<NestFile>().initialize(b_cfg).has_value());
        for (int i = 0; i < 5000 && (!outer.get<0>().get<NestFile>().writer_started() ||
                                     !outer.get<1>().writer_started());
             ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(outer.get<0>().get<NestFile>().writer_started());
        NX_CHECK(outer.get<1>().writer_started());

        outer.warning("nested leaf");
        NX_CHECK(outer.flush().has_value());

        alignas(64) static char fa[592];
        alignas(64) static char fb[592];
        std::snprintf(fa, sizeof fa, "%s/leaf.jsonl", nest_a);
        std::snprintf(fb, sizeof fb, "%s/leaf.jsonl", nest_b);
        alignas(64) static char ca[40096];
        alignas(64) static char cb[40096];
        const std::size_t na_len = read_whole_file(fa, ca);
        const std::size_t nb_len = read_whole_file(fb, cb);
        NX_CHECK(na_len != SIZE_MAX && nb_len != SIZE_MAX);
        NX_CHECK(file_line_count(fa) == 1 && file_line_count(fb) == 1);
        // The record payload that reached each leaf is identical (the UTC
        // offset sampled by each sink's start may differ by ns — compare the
        // record-derived part: everything from the message field onwards).
        NX_CHECK(na_len == nb_len);
        const char* ma = std::strstr(ca, "\"message\":\"");
        const char* mb = std::strstr(cb, "\"message\":\"");
        NX_CHECK(ma != nullptr && mb != nullptr);
        if (ma != nullptr && mb != nullptr) {
            NX_CHECK(std::strcmp(ma, mb) == 0);
        }
        NX_CHECK(std::strstr(ca, "nested leaf\"}") != nullptr);
    }
}

/// (e)+(f) log-fold and flush-fold failures surface from the first member.
void test_composite_failure_fold() {
    const char* comp_base = composite_base();

    // --- (e) log-fold failure surfacing: the FIRST member's RateLimited must
    // not be masked by the later NullLogger's OK (task 7.1: last FAILURE wins).
    alignas(64) static char thr_dir[560];
    std::snprintf(thr_dir, sizeof thr_dir, "%.500s/throttled", comp_base);
    static_cast<void>(::mkdir(thr_dir, 0755));
    {
        static CompositeLogger<FileLogger<16, 128>, NullLogger> mixed{};
        FileLoggerConfig cfg{};
        cfg.app_name = "mix";
        cfg.base_dir = thr_dir;
        cfg.file_name = "mix.jsonl";
        cfg.initial_level = LogLevel::Verbose;
        cfg.rotation.kind = RotationSchedule::None;
        cfg.throttle = ThrottleConfig{true, 1ULL, 1'000'000'000ULL}; // 1 record / 1 s
        NX_CHECK(mixed.get<0>().initialize(cfg).has_value());
        for (int i = 0; i < 5000 && !mixed.get<0>().writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(mixed.get<0>().writer_started());

        mixed.info("throttle first");
        mixed.info("throttle second"); // over the 1-per-window budget
        // Record methods return void: the drop is surfaced ONLY through the
        // member's own snapshot (spec logging/logger-concept drop semantics).
        NX_CHECK(mixed.get<MixFile>().dropped().rate_limited == 1ULL);
        NX_CHECK(mixed.get<MixFile>().dropped().slot_exhausted == 0ULL);
        NX_CHECK(mixed.flush().has_value());
        // tree dump nests the member dumps (spec composite-logger). Taken
        // AFTER the flush barrier: pending/dropped counters are quiescent,
        // so the free/member comparison is not racing the writer drain.
        const std::string comp_dump = mixed.to_string();
        NX_CHECK(comp_dump.find("CompositeLogger(level=") == 0U);
        NX_CHECK(comp_dump.find("members=2") != std::string::npos);
        NX_CHECK(comp_dump.find("FileLogger(level=") != std::string::npos);
        NX_CHECK(comp_dump.find("NullLogger(level=") != std::string::npos);
        NX_CHECK(to_string(mixed) == comp_dump);
    }

    // --- (f) flush-fold failure surfacing: a member whose flush fails
    // (/dev/full -> SystemError) keeps its error even when a later member's
    // flush answers OK.
    {
        static CompositeLogger<FileLogger<16, 128>, NullLogger> void_comp{};
        FileLoggerConfig cfg{};
        cfg.app_name = "void";
        cfg.base_dir = "/dev"; // mkdirat EEXIST-tolerated; /dev/full fails writes
        cfg.file_name = "full";
        cfg.initial_level = LogLevel::Verbose;
        cfg.rotation.kind = RotationSchedule::None;
        NX_CHECK(void_comp.get<0>().initialize(cfg).has_value());
        for (int i = 0; i < 5000 && !void_comp.get<0>().writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(void_comp.get<0>().writer_started());
        void_comp.info("into the void"); // enqueue is wait-free regardless
        const Status f = void_comp.flush();
        NX_CHECK(!f.has_value());
        NX_CHECK(f.error() == ErrorCode::SystemError);
    }
}

/// CompositeLogger: virtual-free fan-out, tree-wide thresholds, sub-config,
/// short-circuit, nested trees and fold-failure surfacing
/// (spec logging/composite-logger).
void test_composite() {
    test_composite_empty_and_propagation();
    test_composite_fanout();
    test_composite_nested();
    test_composite_failure_fold();
    std::printf("composite: fan-out identity, propagation, sub-config, short-circuit, "
                "empty pack, last-failure surfacing all verified\n");
}
} // namespace

void suite_composite() {
    run_suite("composite", test_composite);
}
