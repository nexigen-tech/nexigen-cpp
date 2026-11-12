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
//   12  level          core::LogLevel (u8)     severity
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
/// @brief Default inline argument-blob size of `LogRecord`, in bytes.
///
/// Define `NEXIGEN_LOG_MESSAGE_CAPACITY` before including this header (for
/// example `-DNEXIGEN_LOG_MESSAGE_CAPACITY=256`) to override the default; when
/// it is left undefined the blob holds 128 bytes. The value must stay within
/// the `LogRecord` static assertion's `[16, 0xFFFF]` window.
#define NEXIGEN_LOG_MESSAGE_CAPACITY 128
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/logging/detail/arg_pack.hpp>
#include <nexigen/core/log_level.hpp>

namespace nexigen::logging {

/// @brief Type-erased render callback invoked on the consumer thread.
///
/// Reads the record's format pointer and argument blob, renders into `out` with
/// at most `cap` bytes, and returns the bytes written. The signature is matched
/// exactly by `detail::render_impl<MessageCapacity, Args...>`, generated at
/// compile time for each argument pack; the producer never invokes it.
///
/// @note Spec `logging/log-record`, requirement "Deferred type-erased argument
///       formatting" and requirement "Consumer-side rendering of packed records".
using record_render_fn = std::size_t (*)(const char* fmt, const char* blob,
                                         std::uint16_t blob_length, char* out,
                                         std::size_t cap) noexcept;

/// @brief Fixed-capacity, cache-line-aligned record crossing the shard boundary.
///
/// The value type stored in the per-thread SPSC shards. It is trivially
/// copyable and owns no heap memory: the format-string pointer, the location
/// pointers and the type-erased render callback all reference program-static or
/// record-owned storage, and string-like arguments are copied into
/// `arg_payload_` at emit time. The producer only packs; the consumer renders.
///
/// @tparam MessageCapacity Inline argument-blob size in bytes; must lie in
///         `[16, 0xFFFF]` so `arg_length` can address it. Defaults to
///         `NEXIGEN_LOG_MESSAGE_CAPACITY`.
/// @note Spec `logging/log-record`, requirement "Fixed-size trivially-copyable
///       record".
/// @note Spec `logging/multi-producer`, requirement "Pre-allocated per-thread
///       shard array": one record lives in exactly one shard owned by one
///       producer thread.
template <std::size_t MessageCapacity = NEXIGEN_LOG_MESSAGE_CAPACITY> struct alignas(64) LogRecord {
    static_assert(MessageCapacity >= 16 && MessageCapacity <= 0xFFFF,
                  "argument blob must be positive and addressable by uint16 length");

    /// @brief Report the compile-time inline argument-blob size.
    ///
    /// @return `MessageCapacity`, the usable bytes of `arg_payload_`.
    /// @note Spec `logging/log-record`, requirement "Fixed-size trivially-copyable
    ///       record": the size is a compile-time constant with no heap indirection.
    [[nodiscard]] static constexpr std::size_t message_capacity() noexcept {
        return MessageCapacity;
    }

    /// @brief Render the deferred format into caller-provided scratch.
    ///
    /// Consumer-side only: invokes `render` with this record's format pointer and
    /// argument blob. A default record (no callback) or a zero-capacity buffer
    /// yields an empty message. The returned length is authoritative — sinks cap
    /// their copy at it, so truncated text is complete-or-bounded, never
    /// half-emitted downstream.
    ///
    /// @param out Destination scratch; must hold at least `cap` writable bytes.
    /// @param cap Capacity of `out` in bytes; `0` disables rendering.
    /// @return Bytes written into `out`, in `[0, cap]`.
    /// @note Sets the render-truncation flag when the output consumed the whole
    ///       buffer. Non-const by design: the flag is written in the consumer's
    ///       own popped copy, never in a producer-visible slot.
    /// @note Spec `logging/log-record`, requirement "Consumer-side rendering of
    ///       packed records", scenario "Render overflow is bounded".
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

    /// @brief Test whether the packer had to shorten an inline argument copy.
    ///
    /// @return `true` when the argument blob was truncated at pack time (bit 0
    ///         of `flags`), so the rendered message is shorter than the call
    ///         site's full argument text.
    /// @note Spec `logging/log-record`, requirement "Fixed-size trivially-copyable
    ///       record", scenario "String-like arguments are copied into
    ///       record-owned storage".
    [[nodiscard]] bool args_truncated() const noexcept {
        return (flags & detail::flag_args_truncated) != 0U;
    }
    /// @brief Test whether consumer rendering ran out of scratch capacity.
    ///
    /// @return `true` when the last `render_into()` filled its whole buffer (bit
    ///         1 of `flags`).
    [[nodiscard]] bool render_truncated() const noexcept {
        return (flags & detail::flag_render_truncated) != 0U;
    }

    /// @brief Render a raw record dump for cold-path diagnostics.
    ///
    /// @return Stamp, level, thread id, location triplet, format text, used
    ///         argument bytes and the raw flags — not the rendered message.
    /// @note Mandate 9 (`to_string`), and spec `logging/log-record`, requirement
    ///       "LogRecord diagnostic string"; for a rendered view, format into
    ///       scratch first and inspect that buffer. Never call this on a hot path.
    [[nodiscard]] std::string to_string() const {
        std::string out = "LogRecord(";
        out += "stamp=" + chrono::to_string(stamp);
        out += ", level=" + std::string(core::to_string(level));
        out += ", thread_id=" + std::to_string(thread_id);
        out += ", file=" + std::string(file_name != nullptr ? file_name : "");
        out += ':' + std::to_string(line);
        out += ", function=" + std::string(function_name != nullptr ? function_name : "");
        out += ", fmt=" + std::string(fmt != nullptr ? fmt : "");
        out += ", arg_bytes=" + std::to_string(arg_length);
        out += ", flags=" + std::to_string(flags) + ")";
        return out;
    }

    chrono::Timestamp stamp{};                  ///< Monotonic nanoseconds, chrono domain.
    std::uint32_t thread_id{};                  ///< Cached Linux `SYS_gettid` of the producer.
    core::LogLevel level{core::LogLevel::Info}; ///< Severity carried by the record.
    std::uint8_t flags{};                       ///< Bit 0 args trunc; bit 1 render trunc.
    std::uint16_t arg_length{};                 ///< Used bytes of `arg_payload_` (per pack).
    const char* file_name{};                    ///< `source_location::file_name`, static.
    const char* function_name{};                ///< `source_location::function_name`, static.
    std::uint32_t line{};                       ///< `source_location::line` of the call site.
    const char* fmt{};                          ///< Checked format literal, static storage.
    record_render_fn render{};                  ///< Pack renderer (`nullptr` = default record).
    /// @brief Inline argument blob, 8-aligned so it starts at offset 56.
    ///
    /// Holds the packed, trivially copyable argument bytes; string-like values
    /// are copied in and truncated to capacity when too long. The layout matches
    /// the design D4 offset table.
    alignas(std::uint64_t) std::array<char, MessageCapacity> arg_payload_{};
};

/// @brief Free helper mirroring `LogRecord::to_string` for generic call sites.
///
/// @tparam MessageCapacity Inline argument-blob size of the record being dumped.
/// @param rec Record to render; may be a default-populated record.
/// @return The same text as `rec.to_string()`.
/// @note Mandate 9 (`to_string`), and spec `logging/log-record`, requirement
///       "LogRecord diagnostic string".
template <std::size_t MessageCapacity = NEXIGEN_LOG_MESSAGE_CAPACITY>
[[nodiscard]] std::string to_string(const LogRecord<MessageCapacity>& rec) {
    return rec.to_string();
}

} // namespace nexigen::logging
