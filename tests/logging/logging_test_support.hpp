// Shared harness and fixtures for the nexigen::logging test suite.
//
// Split per entity (levels, records, producer, latency, sinks, rotation,
// settings, worker, stress, ...): an edit recompiles one small TU, a failure
// names the owning file, and diffs stay reviewable. The allocation-free arena
// and the free-store override live in logging_test_support.cpp.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <format>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <malloc.h>  // mallinfo2(): glibc-счётчики кучи (Linux-only проект)
#include <pthread.h> // cpu_set_t + affinity queries for the benchmark harness placement

#include <nlohmann/json.hpp>

#include "../support/test_temp.hpp"

#include <nexigen/core/result.hpp>
#include <nexigen/core/detail/fault_injection.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/composite_logger.hpp>
#include <nexigen/logging/console_logger.hpp>
#include <nexigen/logging/detail/affinity.hpp>
#include <nexigen/logging/detail/async_worker.hpp>
#include <nexigen/logging/detail/json_writer.hpp>
#include <nexigen/logging/detail/level_control.hpp>
#include <nexigen/logging/detail/producer.hpp>
#include <nexigen/logging/detail/rotation.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/log_record.hpp>
#include <nexigen/logging/logger_options.hpp>
#include <nexigen/logging/settings.hpp>
#include <nexigen/logging/null_logger.hpp>

using namespace nexigen::core;   // test TU: exercise core unqualified
using namespace nexigen::chrono; // record stamps/clock reads come from the chrono module
using namespace nexigen::logging;
namespace core = nexigen::core;     // also allow explicit core:: qualification in fixtures
namespace chrono = nexigen::chrono; // and explicit chrono:: qualification (stamps)

// --- Compile-time claims (task 2.1 / spec logging/log-level): severity order
// is the declaration order, and every lowercase name resolves in constexpr.
static_assert(LogLevel::Verbose < LogLevel::Debug);
static_assert(LogLevel::Debug < LogLevel::Info);
static_assert(LogLevel::Info < LogLevel::Warning);
static_assert(LogLevel::Warning < LogLevel::Error);
static_assert(LogLevel::Error < LogLevel::Fatal);
static_assert(LogLevel::Fatal < LogLevel::Off);
static_assert(to_string(LogLevel::Verbose) == "verbose");
static_assert(to_string(LogLevel::Debug) == "debug");
static_assert(to_string(LogLevel::Info) == "info");
static_assert(to_string(LogLevel::Warning) == "warning");
static_assert(to_string(LogLevel::Error) == "error");
static_assert(to_string(LogLevel::Fatal) == "fatal");
static_assert(to_string(LogLevel::Off) == "off");
static_assert(to_string(static_cast<LogLevel>(200)) == "Unknown");

// --- Compile-time claims (task 4.3 / design D4): deferred-format record shape
// and pinned layout (format pointer + render callback + inline argument blob).
static_assert(sizeof(LogRecord<128>) == 192);
static_assert(alignof(LogRecord<128>) == 64);
static_assert(std::is_trivially_copyable_v<LogRecord<128>>);
static_assert(std::is_default_constructible_v<LogRecord<128>>);
static_assert(std::is_move_assignable_v<LogRecord<128>>);
static_assert(offsetof(LogRecord<128>, stamp) == 0);
static_assert(offsetof(LogRecord<128>, thread_id) == 8);
static_assert(offsetof(LogRecord<128>, level) == 12);
static_assert(offsetof(LogRecord<128>, flags) == 13);
static_assert(offsetof(LogRecord<128>, arg_length) == 14);
static_assert(offsetof(LogRecord<128>, file_name) == 16);
static_assert(offsetof(LogRecord<128>, function_name) == 24);
static_assert(offsetof(LogRecord<128>, line) == 32);
static_assert(offsetof(LogRecord<128>, fmt) == 40);
static_assert(offsetof(LogRecord<128>, render) == 48);
static_assert(offsetof(LogRecord<128>, arg_payload_) == 56);

// --- Compile-time claims (task 4.1/4.2 / design D3/D5): FormatWithLocation is
// constructible ONLY from literals (consteval), the literal call shapes deduce
// as expected, and the Packable surface accepts exactly the documented set.
static_assert(std::is_trivially_destructible_v<FormatWithLocation<>>);
static_assert(requires { FormatWithLocation<>{"plain"}; });
static_assert(requires { FormatWithLocation<double>{"{:.4f}"}; });
static_assert(detail::Packable<double> && detail::Packable<int> && detail::Packable<bool> &&
              detail::Packable<std::string_view> && detail::Packable<LogLevel> &&
              detail::Packable<Timestamp> && detail::Packable<const char*>);
static_assert(!detail::Packable<std::string> && !detail::Packable<double*>);

inline int g_checks = 0;
inline int g_failures = 0;

// Allocation detector (design D18): the C++ free store is re-backed by a
// fixed static bump arena; any operator new anywhere in the process bumps
// g_alloc_used. 1 MiB covers std::jthread stop-state bookkeeping across every
// logger the suite constructs; hot-path code must leave both counters flat.
extern std::size_t g_alloc_used;

// Compile-away proof helper (declared here, defined in null_logger_test.cpp):
// the emitted body must contain no logging call.
int null_loop(int iterations) noexcept;

#define NX_CHECK(condition)                                                                        \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(condition)) {                                                                        \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);              \
        }                                                                                          \
    } while (false)

inline void run_suite(const char* suite_name, void (*body)()) {
    const int checks_before = g_checks;
    const int failures_before = g_failures;

    body();

    const int suite_checks = g_checks - checks_before;
    const int suite_failures = g_failures - failures_before;
    std::printf("[%-24s] %s (%d checks, %d failed)\n", suite_name,
                suite_failures == 0 ? "OK" : "FAILED", suite_checks, suite_failures);
}

// Heap in use (bytes handed to malloc and not freed): the raw-malloc twin of
// the arena counter above. Windows measure before/around steady-state phases.
[[nodiscard]] inline std::size_t heap_in_use() noexcept {
    return static_cast<std::size_t>(mallinfo2().uordblks);
}

// Harness self-check (mirrors core_test task 1.5 energy): proves NX_CHECK,
// run_suite and both allocation counters are wired before real suites land.
inline void test_harness() {
    NX_CHECK(g_checks > 0); // this very suite is counting
    // NB: no assumption about heap_in_use() magnitude — under ASan the glibc
    // heap is empty by design (the sanitizer owns it); only deltas are checked.
    const std::size_t arena_before = g_alloc_used;
    const std::size_t heap_before = heap_in_use();
    { // a deliberate free-store user: the arena counter must move...
        void* p = ::operator new(64U);
        ::operator delete(p);
    }
    NX_CHECK(g_alloc_used == arena_before + 64U);
    // ...while the raw heap counter stays put (arena absorbs C++ allocations).
    NX_CHECK(heap_in_use() == heap_before);
}

// Drain a pipe to EOF (write ends must be closed by caller first).
inline std::size_t read_all(int fd, char* buf, std::size_t cap) {
    std::size_t n = 0;
    for (;;) {
        const ssize_t r = ::read(fd, buf + n, cap - 1 - n);
        if (r <= 0) {
            break;
        }
        n += static_cast<std::size_t>(r);
        if (n + 1 >= cap) {
            break;
        }
    }
    buf[n] = '\0';
    return n;
}

inline std::size_t count_char(const char* s, std::size_t n, char c) {
    std::size_t k = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (s[i] == c) {
            ++k;
        }
    }
    return k;
}

// Count newlines in a file; SIZE_MAX if the file cannot be opened.
inline std::size_t file_line_count(const char* path) {
    const int fdr = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fdr < 0) {
        return SIZE_MAX;
    }
    char chunk[4096];
    std::size_t lines = 0;
    for (;;) {
        const ssize_t n = ::read(fdr, chunk, sizeof chunk);
        if (n <= 0) {
            break;
        }
        for (ssize_t i = 0; i < n; ++i) {
            if (chunk[i] == '\n') {
                ++lines;
            }
        }
    }
    ::close(fdr);
    return lines;
}

// Read up to cap-1 bytes of a file (small fixtures).
inline std::size_t read_whole_file(const char* path, char (&buf)[40096]) {
    const int fdr = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fdr < 0) {
        return SIZE_MAX;
    }
    const ssize_t n = ::read(fdr, buf, sizeof buf - 1);
    ::close(fdr);
    if (n < 0) {
        return SIZE_MAX;
    }
    buf[n] = '\0';
    return static_cast<std::size_t>(n);
}

// --- drop-summary fixtures (spec logging/multi-producer): the sink collects
// rendered lines into a caller-owned buffer shared with the test; the
// barrier's release/acquire on `drained` orders every handle() before flush()
// returns, and probes read only while the worker is idle or joined.
template <class RecordT> struct LinesSink {
    LinesSink(char* buffer, std::size_t cap) noexcept : buf_(buffer), cap_(cap) {}

    [[nodiscard]] core::Status start() noexcept { return {}; }
    void handle(RecordT& rec) noexcept {
        char msg[256];
        const std::size_t n = rec.render_into(msg, sizeof msg);
        if (n == 0U || used_ >= cap_) {
            return;
        }
        std::size_t take = n;
        if (used_ + take > cap_) {
            take = cap_ - used_;
        }
        std::memcpy(buf_ + used_, msg, take);
        used_ += take;
        if (used_ < cap_) {
            buf_[used_++] = '\n';
        }
    }
    [[nodiscard]] core::Status synchronize() noexcept { return {}; }
    void stop() noexcept {}

    // Barrier-ordered reads (after flush()/join): the zero-padded tail beyond
    // used_ is safe to scan — find/count over the full buffer cannot match it.
    [[nodiscard]] bool find(std::string_view text) const noexcept {
        return std::string_view(buf_, cap_).find(text) != std::string_view::npos;
    }
    [[nodiscard]] std::size_t count(std::string_view text) const noexcept {
        std::size_t hits = 0;
        const std::string_view view(buf_, cap_);
        for (std::size_t pos = view.find(text); pos != std::string_view::npos;
             pos = view.find(text, pos + 1U)) {
            ++hits;
        }
        return hits;
    }

private:
    char* buf_{};
    std::size_t cap_ = 0;
    std::size_t used_ = 0;
};

// --- logging_settings helpers (change add-logging-settings-loader)
template <class T, class = void> struct has_from_json : std::false_type {};
template <class T>
struct has_from_json<
    T, std::void_t<decltype(from_json(std::declval<const nlohmann::json&>(), std::declval<T&>()))>>
    : std::true_type {};

static_assert(!has_from_json<Settings>::value, "Settings is factory/IO-bound: to_json only");
static_assert(has_from_json<core::SettingsDiagnostic>::value,
              "core::SettingsDiagnostic must round-trip through JSON");
static_assert(has_from_json<core::SettingsPaths>::value,
              "core::SettingsPaths must round-trip through JSON");

struct EnvGuard {
    std::string name;
    bool had{false};
    std::string saved;

    explicit EnvGuard(const char* variable_name) : name(variable_name) {
        const char* const value = std::getenv(variable_name);
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
    out.close();
    NX_CHECK(out.good());
}

inline std::string basename_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1U);
}

inline std::vector<std::string> list_regular_files(const std::string& dir) {
    std::vector<std::string> files;
    DIR* const handle = ::opendir(dir.c_str());
    if (handle == nullptr) {
        return files;
    }
    while (dirent* const entry = ::readdir(handle)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        const std::string full = dir + "/" + name;
        struct stat st{};
        if (::stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            files.push_back(full);
        }
    }
    static_cast<void>(::closedir(handle));
    return files;
}

inline std::size_t line_count(const std::string& path) {
    std::ifstream in{path};
    std::size_t lines = 0;
    std::string line;
    while (std::getline(in, line)) {
        ++lines;
    }
    return lines;
}

inline bool has_basename(const std::vector<std::string>& files, std::string_view name) {
    for (const std::string& file : files) {
        if (basename_of(file) == name) {
            return true;
        }
    }
    return false;
}

inline std::size_t total_lines(const std::vector<std::string>& files) {
    std::size_t total = 0;
    for (const std::string& file : files) {
        total += line_count(file);
    }
    return total;
}

// --- worker idle helpers (spec: poll interval bounds idle wake-ups).
/// Thread ids of this process (cold diagnostic helper).
inline std::vector<long> process_tids() {
    std::vector<long> tids;
    if (DIR* dir = ::opendir("/proc/self/task")) {
        while (const dirent* entry = ::readdir(dir)) {
            if (entry->d_name[0] != '.') {
                tids.push_back(std::strtol(entry->d_name, nullptr, 10));
            }
        }
        ::closedir(dir);
    }
    return tids;
}

/// voluntary_ctxt_switches of one thread, -1 when unreadable.
inline long voluntary_switches(long tid) {
    std::ifstream status("/proc/self/task/" + std::to_string(tid) + "/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("voluntary_ctxt_switches:", 0) == 0) {
            return std::strtol(line.c_str() + 24, nullptr, 10);
        }
    }
    return -1;
}

/// Wake-ups of the worker thread (the one thread that appeared after `before`)
/// over `window_ms` of idleness.
template <class Logger, class Config>
long idle_wakeups(Logger& logger, const Config& cfg, int window_ms) {
    const std::vector<long> before = process_tids();
    NX_CHECK(logger.initialize(cfg).has_value());
    for (int i = 0; i < 5000 && !logger.writer_started(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    long worker = -1;
    for (const long tid : process_tids()) {
        if (std::find(before.begin(), before.end(), tid) == before.end()) {
            worker = tid;
        }
    }
    NX_CHECK(worker > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // let backoff settle
    const long start = voluntary_switches(worker);
    std::this_thread::sleep_for(std::chrono::milliseconds(window_ms));
    const long end = voluntary_switches(worker);
    return (start < 0 || end < 0) ? -1 : end - start;
}

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
inline bool logger_fail_scheduler(nexigen::core::fault_injection::FaultPoint point) noexcept {
    return point == nexigen::core::fault_injection::FaultPoint::Thread_set_scheduler;
}
#endif
