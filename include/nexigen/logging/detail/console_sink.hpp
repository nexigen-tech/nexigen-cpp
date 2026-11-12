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

/// @brief Consumer-thread writer for the console.
///
/// Renders each drained record's deferred format into fixed scratch, builds a
/// plain single-line diagnostic (epoch seconds with nanosecond fraction,
/// lowercase level, `basename:line`, `(function)`, message), and writes it to
/// the severity-routed standard stream, tolerating partial writes and `EINTR`.
/// All storage is pre-allocated, so the sink never allocates; instances are
/// constructed in place by the worker on the consumer thread and are never
/// touched by producers.
///
/// @note Spec `logging/console-logger`, requirement "Asynchronous wait-free
///       console output" and requirement "Console line content".
class ConsoleSink {
    /// @brief Capacity of the staged output line, including the newline.
    static constexpr std::size_t line_capacity = 512;
    /// @brief Capacity of the deferred-format render buffer, in bytes.
    static constexpr std::size_t msg_capacity = 256;

    /// @brief Pre-allocated line scratch; consumer-only, never reallocated.
    char line_[line_capacity]{};
    /// @brief Pre-allocated deferred-render scratch; consumer-only.
    char msg_[msg_capacity]{};
    /// @brief Bytes currently staged in `line_`.
    std::size_t len_ = 0;

    /// @brief Bounded appender: write only if space remains; never reallocates.
    ///
    /// @param c Byte to append; dropped when the line is full.
    void append_char(char c) noexcept {
        if (len_ < line_capacity - 1) {
            line_[len_++] = c;
        }
    }
    /// @brief Bounded string-view appender, truncating at the line capacity.
    ///
    /// @param sv Bytes to append; the copy stops at the last writable slot.
    void append_sv(std::string_view sv) noexcept {
        const std::size_t room = (len_ < line_capacity - 1) ? (line_capacity - 1 - len_) : 0;
        const std::size_t take = sv.size() < room ? sv.size() : room;
        for (std::size_t i = 0; i < take; ++i) {
            line_[len_++] = sv[i];
        }
    }
    /// @brief Append an unsigned decimal value, zero-padded to a minimum width.
    ///
    /// @param value Value to render.
    /// @param min_width Minimum digit count; extra leading zeros are appended.
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

    /// @brief Return the final path component of `path`.
    ///
    /// @param path Source path as recorded in the record.
    /// @return The substring after the last '/', or `path` when it has none.
    static std::string_view basename(std::string_view path) noexcept {
        const std::size_t slash = path.find_last_of('/');
        return (slash == std::string_view::npos) ? path : path.substr(slash + 1);
    }

    /// @brief Write `bytes` of `line_` to `fd`, retrying partial writes/EINTR.
    ///
    /// @param fd Routed standard-stream descriptor.
    /// @param bytes Number of staged bytes to write.
    /// @note A closed tty or a non-`EINTR` error ends the loop: the loss is
    ///       consumer-side only and never propagates to a producer.
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
    /// @brief Enter the sink contract; the console needs no cold start.
    ///
    /// @return `Status`: always OK, since the buffers are already resident.
    /// @note Spec `logging/console-logger`, requirement "Asynchronous wait-free
    ///       console output".
    [[nodiscard]] core::Status start() noexcept { return {}; }
    /// @brief Leave the sink contract; nothing is owned by the console sink.
    void stop() noexcept {}

    /// @brief No-op synchronization: an unbuffered `::write` is already done.
    ///
    /// @return `Status`: always OK, because consoles have no fsync target.
    /// @note Spec `logging/console-logger`, requirement "Flush drains
    ///       asynchronously-accepted records"; the worker's flush barrier
    ///       provides the ordering, this sink has nothing left to sync.
    [[nodiscard]] core::Status synchronize() noexcept { return {}; }

    /// @brief Render one record and write its console line.
    ///
    /// Deferred formatting executes here on the consumer thread into `msg_`,
    /// allocation-free and bounded. Levels at or above `Warning` go to
    /// `STDERR_FILENO`, all lower levels to `STDOUT_FILENO`.
    ///
    /// @tparam Record Record type exposing `render_into`, `stamp`, `level`,
    ///         `file_name`, `line`, and `function_name`.
    /// @param rec Drained record; read only, never retained.
    /// @note Spec `logging/console-logger`, requirement "Console line content"
    ///       and requirement "Stream routing by severity"; deferred rendering
    ///       per `logging/log-record`, requirement "Consumer-side rendering of
    ///       packed records". The level name goes through `to_string`
    ///       (mandate 9).
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
