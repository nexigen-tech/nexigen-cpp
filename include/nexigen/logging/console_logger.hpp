// nexigen::logging — asynchronous console sink (design.md D13, spec
// logging/console-logger).
//
// Pipeline: detail::Producer (wait-free enqueue, single producer thread) +
// detail::Async_worker (std::jthread drain loop). Records are formatted on
// the consumer thread into a fixed line buffer and written with raw
// ::write() to fd 1 / fd 2 — deliberately NOT std::cout/printf, whose lazy
// initialization, stream buffering, and locale state allocate and can block.
//
// Routing (spec "Stream routing by severity"): Verbose/Debug/Info -> stdout;
// Warning/Error/Fatal -> stderr.
//
// Line shape (self-descriptive, fixed format, allocation-free):
//   "<monotonic-sec>.<ns9> <level> <file basename>:<line> (<function>) <message>\n"
// The timestamp is the record's monotonic-domain stamp (CLOCK_MONOTONIC_RAW
// ns). Wall-clock ISO-8601 belongs to the file sink's calibrated formatter;
// the console line only promises "includes the timestamp" (spec scenario).
//
// CONTRACT (mirrors core::SpscRingBuffer's header): exactly one producer
// thread calls log()/conveniences on a given instance; the internal worker
// thread is the sole consumer. set_level()/level()/dropped()/ring_size() are
// safe from any thread (relaxed snapshots — diagnostics only). flush() is a
// control-plane barrier (drain + publish) and sits outside the never-block
// guarantee that binds the record methods.
//
// DROP SEMANTICS (returned Status of log()/conveniences):
//   {}              record accepted into the ring, OR filtered below the
//                   active level (filtering is normal operation, not a loss)
//   BufferFull      ring full: nothing enqueued, counted in dropped()
//   RateLimited     never — the console sink configures no throttle
//
// Usage rule (see core/result.hpp): never call .value() on a returned Status.
#pragma once

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/detail/async_logger.hpp>
#include <nexigen/logging/detail/producer.hpp>
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/log_record.hpp>

namespace nexigen::logging::detail {

/// Consumer-thread writer for the console: formats a plain line into fixed
/// storage, writes it to the routed fd, tolerates partial writes/EINTR.
class Console_sink {
    static constexpr std::size_t line_capacity = 512;

    char line_[line_capacity]{}; ///< pre-allocated line scratch (consumer-only)
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
    void handle(const Record& rec) noexcept {
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
        append_sv(rec.message_view());
        append_char('\n');
        const int fd = (rec.level >= LogLevel::Warning) ? STDERR_FILENO : STDOUT_FILENO;
        write_all(fd, len_);
    }
};

} // namespace nexigen::logging::detail

namespace nexigen::logging {

/// Asynchronous wait-free console logger. Template parameters fix the ring
/// capacity and inline message size at compile time (SPSC power-of-two
/// contract enforced by the queue itself).
template <std::size_t Capacity = 4096, std::size_t MessageCapacity = 128>
class ConsoleLogger {
    using Producer = detail::Producer<Capacity, MessageCapacity>;

    Producer producer_{};
    detail::Async_worker<Producer, detail::Console_sink> worker_{};

public:
    struct Config {
        LogLevel initial_level{LogLevel::Info};
    };

    ConsoleLogger() noexcept = default;
    ConsoleLogger(const ConsoleLogger&) = delete;
    ConsoleLogger& operator=(const ConsoleLogger&) = delete;
    ConsoleLogger(ConsoleLogger&&) = delete;
    ConsoleLogger& operator=(ConsoleLogger&&) = delete;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

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

    void set_level(LogLevel new_level) noexcept { producer_.set_level(new_level); }
    [[nodiscard]] LogLevel level() const noexcept { return producer_.level(); }

    [[nodiscard]] core::Status log(LogLevel level, std::source_location loc,
                                   std::string_view message) noexcept {
        return producer_.emit(level, loc, message);
    }

    [[nodiscard]] core::Status verbose(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Verbose, loc, msg);
    }
    [[nodiscard]] core::Status debug(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Debug, loc, msg);
    }
    [[nodiscard]] core::Status info(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Info, loc, msg);
    }
    [[nodiscard]] core::Status warning(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Warning, loc, msg);
    }
    [[nodiscard]] core::Status error(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Error, loc, msg);
    }
    [[nodiscard]] core::Status fatal(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Fatal, loc, msg);
    }

    /// Control-plane barrier: returns once every record accepted before the
    /// call has been written to its stream (console writes are unbuffered
    /// syscalls, so synchronize() is trivially complete at write time).
    [[nodiscard]] core::Status flush() noexcept { return worker_.flush(); }

    /// Third-thread diagnostics (snapshots, not hot-path synchronization).
    [[nodiscard]] Dropped_stats dropped() const noexcept { return producer_.dropped(); }
    [[nodiscard]] std::size_t pending() const noexcept { return producer_.ring_size(); }
    [[nodiscard]] bool writer_started() const noexcept { return worker_.writer_started(); }
};

} // namespace nexigen::logging
