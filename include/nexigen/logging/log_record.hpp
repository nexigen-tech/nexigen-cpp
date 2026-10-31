// nexigen::logging — the payload that crosses the producer/consumer boundary
// (change add-serialization-and-wait-free-logging, design D4/D5; spec
// logging/log-record).
//
// LogRecord is the fixed-size, cache-line-aligned, trivially copyable value
// type stored in the per-thread SPSC shards. The record carries a DEFERRED
// format: the raw (compile-time-checked) format-string pointer, a type-erased
// render callback specialized for the argument pack, and the serialized
// argument bytes in an inline blob. The producer never renders (no
// std::format machinery, no float-to-string); the consumer invokes the
// callback into pre-allocated scratch when writing console lines or JSON
// records.
//
// All pointers reference program-static or record-owned storage: format
// literals live in the image, the location strings come from
// std::source_location's static table, and argument bytes were copied into
// the blob at emit time. Nothing here aliases caller memory that could die
// before the record drains (string_view arguments are inlined by the packer).
//
// Pinned layout for MessageCapacity = 200 (sizeof == 256 == 4 cache lines):
//   0   stamp          chrono::Timestamp monotonic ns domain
//   8   thread_id      uint32            cached SYS_gettid
//   12  level          LogLevel (u8)     severity
//   13  flags          uint8             bit0 args truncated, bit1 render truncated
//   14  arg_length     uint16            bytes of arg_payload_ used (fixed per pack)
//   16  file_name      const char*       static storage
//   24  function_name  const char*       static storage
//   32  line           uint32            source line
//   36  (padding)
//   40  fmt            const char*       std::format_string literal (static)
//   48  render         fn ptr            type-erased pack renderer
//   56  arg_payload_   array<char, N>    inline argument blob (8-aligned)
//  256  (tail padding to alignas(64))
//
// MULTI-PRODUCER CONTRACT: a record exists in exactly one shard owned by ONE
// producer thread and drained by the logger's ONE consumer thread (the
// per-thread SPSC sharding of spec logging/multi-producer); slots are handed
// over exclusively through try_push/try_pop. The Status vocabulary of the
// enqueue pipeline (accepted / BufferFull drop / RateLimited drop /
// slot-exhausted drop) is reported through the logger's dropped() snapshot —
// record methods return void (spec logging/logger-concept).
#pragma once

#ifndef NEXIGEN_LOG_MESSAGE_CAPACITY
#define NEXIGEN_LOG_MESSAGE_CAPACITY 128
#endif
/* 
 * Configurable message capacity for LogRecord.
 * Users can define NEXIGEN_LOG_MESSAGE_CAPACITY before including this header
 * (e.g. via -DNEXIGEN_LOG_MESSAGE_CAPACITY=256) to change the default.
 * If undefined, the default is 128.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/logging/detail/arg_pack.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging {

/// Type-erased render callback: reads the record's format pointer + argument
/// blob, writes at most `cap` bytes into `out`, returns bytes written.
/// noexcept; matched exactly by detail::render_impl<MessageCapacity, Args...>.
using record_render_fn = std::size_t (*)(const char* fmt, const char* blob,
                                         std::uint16_t blob_length, char* out,
                                         std::size_t cap) noexcept;

/// Fixed-capacity log record: template over the inline argument blob size.
template <std::size_t MessageCapacity = NEXIGEN_LOG_MESSAGE_CAPACITY>
struct alignas(64) LogRecord {
    static_assert(MessageCapacity >= 16 && MessageCapacity <= 0xFFFF,
                  "argument blob must be positive and addressable by uint16 length");

    [[nodiscard]] static constexpr std::size_t message_capacity() noexcept {
        return MessageCapacity;
    }

    /// Render the deferred format into caller scratch (consumer thread).
    /// Returns bytes written; sets the render-truncation flag when the output
    /// consumed the whole buffer (the text is complete-or-bounded, never
    /// half-emitted downstream because sinks cap at the returned length).
    /// A default record (no render callback) produces an empty message.
    /// Non-const by design: the flag write documents the truncation in the
    /// consumer's own popped copy, never in a producer-visible slot.
    std::size_t render_into(char* out, std::size_t cap) noexcept {
        if (render == nullptr || cap == 0U) {
            return 0U;
        }
        const std::size_t written = render(fmt, arg_payload_.data(), arg_length, out, cap);
        if (written >= cap) {
            flags |= detail::flag_render_truncated;
        }
        return written;
    }

    [[nodiscard]] bool args_truncated() const noexcept {
        return (flags & detail::flag_args_truncated) != 0U;
    }
    [[nodiscard]] bool render_truncated() const noexcept {
        return (flags & detail::flag_render_truncated) != 0U;
    }

    /// Cold-path diagnostic (mandate 9; spec logging/log-record): raw record
    /// dump — stamps/level/location/format text/argument accounting. For a
    /// rendered view, format into scratch first and inspect the buffer.
    [[nodiscard]] std::string to_string() const {
        std::string out = "LogRecord(";
        out += "stamp=" + chrono::to_string(stamp);
        out += ", level=" + std::string(logging::to_string(level));
        out += ", thread_id=" + std::to_string(thread_id);
        out += ", file=" + std::string(file_name != nullptr ? file_name : "");
        out += ':' + std::to_string(line);
        out += ", function=" + std::string(function_name != nullptr ? function_name : "");
        out += ", fmt=" + std::string(fmt != nullptr ? fmt : "");
        out += ", arg_bytes=" + std::to_string(arg_length);
        out += ", flags=" + std::to_string(flags) + ")";
        return out;
    }

    chrono::Timestamp stamp{};                        ///< monotonic nanoseconds
    std::uint32_t thread_id{};                        ///< cached SYS_gettid
    LogLevel level{LogLevel::Info};                   ///< severity
    std::uint8_t flags{};                             ///< bit0 args, bit1 render
    std::uint16_t arg_length{};                       ///< bytes of arg_payload_ in use
    const char* file_name{};                          ///< source_location::file_name
    const char* function_name{};                      ///< source_location::function_name
    std::uint32_t line{};                             ///< source_location::line
    const char* fmt{};                                ///< checked format literal
    record_render_fn render{};                        ///< type-erased pack renderer
    /// 8-aligned so the payload starts at offset 56 (design D4 layout table).
    alignas(std::uint64_t) std::array<char, MessageCapacity> arg_payload_{}; ///< inline pack blob
};

/// Free helper (mandate 9): mirrors the member.
template <std::size_t MessageCapacity = NEXIGEN_LOG_MESSAGE_CAPACITY>
[[nodiscard]] std::string to_string(const LogRecord<MessageCapacity>& rec) {
    return rec.to_string();
}

} // namespace nexigen::logging
