// nexigen::logging — asynchronous console sink (design D13; multi-producer +
// deferred-format rework: change add-serialization-and-wait-free-logging,
// design D6/D7; spec logging/console-logger).
//
// Pipeline: detail::Producer (per-thread wait-free shard writes) +
// detail::Async_worker (std::jthread drain loop). Records carry a DEFERRED
// format: the consumer renders each pack into the sink's pre-allocated line
// scratch (std::vformat_to via the record's type-erased callback), formats
// the plain line, and writes it with raw ::write() to fd 1 / fd 2 —
// deliberately NOT std::cout/printf, whose lazy initialization, stream
// buffering, and locale state allocate and can block.
//
// Routing (spec "Stream routing by severity"): Verbose/Debug/Info -> stdout;
// Warning/Error/Fatal -> stderr.
//
// Line shape (self-descriptive, fixed format, allocation-free):
//   "<monotonic-sec>.<ns9> <level> <file basename>:<line> (<function>) <message>\n"
// The timestamp is the record's monotonic-domain stamp (CLOCK_MONOTONIC_RAW
// ns); <message> is the rendered deferred format. Wall-clock ISO-8601 belongs
// to the file sink's calibrated formatter; the console line only promises
// "includes the timestamp" (spec scenario).
//
// MULTI-PRODUCER CONTRACT (spec logging/multi-producer): up to MaxThreads
// (32 default) distinct producer threads may log to one instance; each thread
// owns one SPSC shard and the internal worker is the sole consumer of all of
// them. set_level()/level()/dropped()/pending()/ring_size() are safe from any
// thread (relaxed snapshots — diagnostics only). flush() is a control-plane
// barrier (drain all shards + publish) and sits outside the never-block
// guarantee that binds the record methods.
//
// DROP SEMANTICS (record methods return void; causes counted per shard and
// aggregated by dropped()): buffer-full when the calling thread's shard is
// full, rate-limited never (console configures no throttle), slot-exhausted
// beyond MaxThreads producers. The console sink renders nothing until
// drained, so drops never surface late in the output stream.
//
// Usage rule (see core/result.hpp): never call .value() on a returned Status.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/detail/async_logger.hpp>
#include <nexigen/logging/detail/producer.hpp>
#include <nexigen/logging/format_with_location.hpp>
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/log_record.hpp>
#include <nexigen/logging/logging_settings.hpp>

namespace nexigen::logging::detail {

/// Consumer-thread writer for the console: renders each record's deferred
/// format into fixed scratch, formats a plain line, writes it to the routed
/// fd, tolerates partial writes/EINTR.
class Console_sink {
    static constexpr std::size_t line_capacity = 512;
    static constexpr std::size_t msg_capacity = 256;

    char line_[line_capacity]{}; ///< pre-allocated line scratch (consumer-only)
    char msg_[msg_capacity]{};   ///< pre-allocated render scratch (consumer-only)
    std::size_t len_ = 0;        ///< bytes currently staged in line_

    /// Bounded appenders: write only if space remains; never reallocate.
    void append_char(char c) noexcept {
        if (len_ < line_capacity - 1) { line_[len_++] = c; }
    }
    void append_sv(std::string_view sv) noexcept {
        const std::size_t room = (len_ < line_capacity - 1) ? (line_capacity - 1 - len_) : 0;
        const std::size_t take = sv.size() < room ? sv.size() : room;
        for (std::size_t i = 0; i < take; ++i) { line_[len_++] = sv[i]; }
    }
    void append_u64(std::uint64_t value, unsigned min_width) noexcept {
        char digits[20];
        std::size_t n = 0;
        do {
            digits[n++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U);
        while (n < min_width) { digits[n++] = '0'; } // zero-pad below line_capacity
        while (n > 0) { append_char(digits[--n]); }
    }

    static std::string_view basename(std::string_view path) noexcept {
        const std::size_t slash = path.find_last_of('/');
        return (slash == std::string_view::npos) ? path : path.substr(slash + 1);
    }

    void write_all(int fd, std::size_t bytes) noexcept {
        std::size_t off = 0;
        while (off < bytes) {
            const ssize_t wrote = ::write(fd, line_ + off, bytes - off);
            if (wrote > 0) {
                off += static_cast<std::size_t>(wrote);
            } else if (errno == EINTR) {
                continue;
            } else {
                return; // tty closed/error: consumer-side loss only (documented)
            }
        }
    }

public:
    [[nodiscard]] core::Status start() noexcept { return {}; }
    void stop() noexcept {}

    /// ::write to an unbuffered fd is complete when it returns: nothing to
    /// fsync for consoles (spec allows flush OK without an fsync target).
    [[nodiscard]] core::Status synchronize() noexcept { return {}; }

    template <class Record>
    void handle(Record& rec) noexcept {
        // Deferred formatting executes HERE (spec logging/log-record):
        // render_into writes the pack through the record's callback into
        // msg_ scratch, allocation-free and bounded, flagging truncation.
        const std::size_t msg_len = rec.render_into(msg_, msg_capacity);

        len_ = 0;
        const std::uint64_t ns_total = rec.stamp.nanoseconds();
        append_u64(ns_total / 1'000'000'000ULL, 1U);
        append_char('.');
        append_u64(ns_total % 1'000'000'000ULL, 9U);
        append_char(' ');
        append_sv(to_string(rec.level));
        append_char(' ');
        append_sv(basename(std::string_view(rec.file_name != nullptr ? rec.file_name : "")));
        append_char(':');
        append_u64(rec.line, 1U);
        append_char(' ');
        append_char('(');
        append_sv(std::string_view(rec.function_name != nullptr ? rec.function_name : ""));
        append_char(')');
        append_char(' ');
        append_sv(std::string_view(msg_, msg_len));
        append_char('\n');
        const int fd = (rec.level >= LogLevel::Warning) ? STDERR_FILENO : STDOUT_FILENO;
        write_all(fd, len_);
    }
};

} // namespace nexigen::logging::detail

namespace nexigen::logging {

/// Construction-time configuration shared by every ConsoleLogger instantiation.
struct ConsoleLogger_config {
    LogLevel initial_level{LogLevel::Info};

    /// Cold-path diagnostic (mandate 9): the configured initial threshold.
    [[nodiscard]] std::string to_string() const {
        return "ConsoleLogger::Config(level=" + std::string(logging::to_string(initial_level)) + ")";
    }

    /// Free form via ADL (nested types are non-deducible for template
    /// helpers; a class-scope friend is the standard discovery route).
    [[nodiscard]] friend std::string to_string(const ConsoleLogger_config& cfg) {
        return cfg.to_string();
    }
};

/// Asynchronous wait-free console logger supporting multiple producer threads
/// via per-thread SPSC shards. Template parameters fix the per-shard capacity,
/// inline message size, and producer pool at compile time (SPSC power-of-two
/// contract enforced by the queue itself).
template <std::size_t ShardCapacity = 128, std::size_t MessageCapacity = 128,
          std::size_t MaxThreads = 32>
class ConsoleLogger {
    using Producer = detail::Producer<ShardCapacity, MessageCapacity, MaxThreads>;

    Producer producer_{};
    detail::Async_worker<Producer, detail::Console_sink> worker_{};

public:
    using Config = ConsoleLogger_config;

    ConsoleLogger() noexcept = default;
    ConsoleLogger(const ConsoleLogger&) = delete;
    ConsoleLogger& operator=(const ConsoleLogger&) = delete;
    ConsoleLogger(ConsoleLogger&&) = delete;
    ConsoleLogger& operator=(ConsoleLogger&&) = delete;

    /// Per-shard capacity; aggregate pool capacity is capacity()*max_threads().
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return ShardCapacity; }
    [[nodiscard]] static constexpr std::size_t max_threads() noexcept { return MaxThreads; }

    /// Initialization phase (pre real-time): calibrate stamps + start the
    /// background writer. No allocations beyond jthread creation internals.
    [[nodiscard]] core::Status initialize(Config cfg = Config{}) noexcept {
        producer_.set_level(cfg.initial_level);
        const core::Status calibrated = producer_.initialize();
        if (!calibrated) {
            return calibrated;
        }
        return worker_.start(producer_, 0ULL); // consoles are not pinned (D15)
    }

    /// Initialize from merged global/local settings (spec logging/logging-settings).
    /// The console sink is deliberately unpinned, so only the initial level and
    /// the explicit capacity check apply.
    [[nodiscard]] core::Status initialize(const Logging_settings& settings) noexcept {
        const core::Status capacity = check_capacity<ShardCapacity, MaxThreads>(settings);
        if (!capacity) {
            return capacity;
        }
        Config cfg{};
        cfg.initial_level = settings.options.initial_log_level;
        return initialize(cfg);
    }

    void set_level(LogLevel new_level) noexcept { producer_.set_level(new_level); }
    [[nodiscard]] LogLevel level() const noexcept { return producer_.level(); }

    /// Variadic surface (spec logging/logger-concept): pack + shard enqueue,
    /// never blocks, never formats, returns void.
    template <class... Args>
    void log(LogLevel level, detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(level, fmt, std::forward<Args>(args)...);
    }

    template <class... Args>
    void verbose(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void debug(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void info(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void warning(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void error(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void fatal(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        producer_.emit(LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    /// Control-plane barrier: returns once every record accepted before the
    /// call has been written to its stream across ALL shards (console writes
    /// are unbuffered syscalls, so synchronize() is trivially complete).
    [[nodiscard]] core::Status flush() noexcept { return worker_.flush(); }

    /// Third-thread diagnostics (snapshots, not hot-path synchronization).
    [[nodiscard]] Dropped_stats dropped() const noexcept { return producer_.dropped(); }
    [[nodiscard]] std::size_t pending() const noexcept { return producer_.ring_size(); }
    [[nodiscard]] bool writer_started() const noexcept { return worker_.writer_started(); }

    /// Cold-path diagnostic (mandate 9): threshold + shard-pool snapshot
    /// (capacity, registered producers, pending, per-cause drops).
    [[nodiscard]] std::string to_string() const {
        return "ConsoleLogger(level=" + std::string(logging::to_string(producer_.level())) +
               ", shard_capacity=" + std::to_string(ShardCapacity) +
               ", shards=" + std::to_string(MaxThreads) +
               ", message_capacity=" + std::to_string(MessageCapacity) +
               ", pending=" + std::to_string(producer_.ring_size()) +
               ", dropped=" + producer_.dropped().to_string() + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
template <std::size_t ShardCapacity, std::size_t MessageCapacity, std::size_t MaxThreads>
[[nodiscard]] std::string to_string(const ConsoleLogger<ShardCapacity, MessageCapacity, MaxThreads>& logger) {
    return logger.to_string();
}

/// Apply merged settings to a console logger config (spec logging/logging-settings
/// "Merged settings apply to logger configuration"): the console sink carries
/// only an initial level today.
[[nodiscard]] inline core::Status apply_to(const Logging_settings& settings,
                                           ConsoleLogger_config& out) noexcept {
    out.initial_level = settings.options.initial_log_level;
    return {};
}

static_assert(concepts::Logger<ConsoleLogger<>>,
              "ConsoleLogger must satisfy the Logger contract (pinned at header level)");

} // namespace nexigen::logging
