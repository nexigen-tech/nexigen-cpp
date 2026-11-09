// nexigen::logging — console writer (consumer thread): renders each record's
// deferred format into fixed scratch, formats a plain line, writes it to the
// routed fd, tolerates partial writes/EINTR (spec logging/console-logger).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <sys/types.h>
#include <unistd.h>

#include <cerrno>

#include <nexigen/core/result.hpp>
#include <nexigen/core/log_level.hpp>

namespace nexigen::logging::detail {

/// Consumer-thread writer for the console: renders each record's deferred
/// format into fixed scratch, formats a plain line, writes it to the routed
/// fd, tolerates partial writes/EINTR.
class ConsoleSink {
    static constexpr std::size_t line_capacity = 512;
    static constexpr std::size_t msg_capacity = 256;

    char line_[line_capacity]{}; ///< pre-allocated line scratch (consumer-only)
    char msg_[msg_capacity]{};   ///< pre-allocated render scratch (consumer-only)
    std::size_t len_ = 0;        ///< bytes currently staged in line_

    /// Bounded appenders: write only if space remains; never reallocate.
    void append_char(char c) noexcept {
        if (len_ < line_capacity - 1) {
            line_[len_++] = c;
        }
    }
    void append_sv(std::string_view sv) noexcept {
        const std::size_t room = (len_ < line_capacity - 1) ? (line_capacity - 1 - len_) : 0;
        const std::size_t take = sv.size() < room ? sv.size() : room;
        for (std::size_t i = 0; i < take; ++i) {
            line_[len_++] = sv[i];
        }
    }
    void append_u64(std::uint64_t value, unsigned min_width) noexcept {
        char digits[20];
        std::size_t n = 0;
        do {
            digits[n++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U);
        while (n < min_width) {
            digits[n++] = '0';
        } // zero-pad below line_capacity
        while (n > 0) {
            append_char(digits[--n]);
        }
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

    template <class Record> void handle(Record& rec) noexcept {
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
        const int fd = (rec.level >= core::LogLevel::Warning) ? STDERR_FILENO : STDOUT_FILENO;
        write_all(fd, len_);
    }
};

} // namespace nexigen::logging::detail
