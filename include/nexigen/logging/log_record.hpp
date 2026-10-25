// nexigen::logging — the payload that crosses the producer/consumer boundary
// (design.md D4).
//
// LogRecord is the fixed-size, cache-line-aligned, trivially copyable value
// type stored in nexigen::core::SpscRingBuffer<LogRecord, Capacity>. Everything
// the consumer needs lives inline: NO indirection into producer-owned or heap
// memory. The std::source_location is decomposed into its (file_name, line,
// function_name) parts at production time: libstdc++'s internal impl pointer
// would otherwise be dereferenced on the consumer, and a source_location value
// itself is not a stable trivially-copyable layout across TUs. The extracted
// pointers reference program-static storage (string literals / the compiler's
// static location table): never freed, allocation-proof, valid for the whole
// process lifetime.
//
// Pinned layout for MessageCapacity = 128 (sizeof == 192 == 3 cache lines):
//   0   stamp          core::Timestamp   monotonic ns domain (design D6)
//   8   thread_id      uint32            cached SYS_gettid (design D5)
//   12  level          LogLevel (u8)     severity
//   13  flags          uint8             bit0 = message truncated
//   14  message_length uint16            bytes of message in use
//   16  file_name      const char*       static storage
//   24  function_name  const char*       static storage
//   32  line           uint32            source line
//   36  (padding)
//   40  message        array<char, N>    inline payload
//  192  (tail padding to alignas(64))
//
// Long messages truncate into `message` (flagged, never spilled); a record is
// copied twice at most (staging -> slot) and both copies are bounded memcpys.
//
// SPSC CONTRACT (mirrors core::SpscRingBuffer's header): a LogRecord exists in
// exactly one ring shared by ONE producer thread and ONE consumer thread;
// slots are handed over exclusively through try_push/try_pop, never aliased
// after the pop beyond the pop's own frame. The Status semantics of the
// enqueue pipeline are the drop table: {} accepted-or-filtered, BufferFull
// when the ring rejected the push, RateLimited when the throttle spent the
// window — see concepts/logger_concept.hpp for the shared wording.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

#include <nexigen/core/timestamp.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging {

/// Fixed-capacity log record: template over the inline message buffer size.
template <std::size_t MessageCapacity>
struct alignas(64) LogRecord {
    static_assert(MessageCapacity >= 16 && MessageCapacity <= 0xFFFF,
                  "message buffer must be positive and addressable by uint16 length");

    static constexpr std::uint8_t flag_truncated = 1U; ///< bit0 of `flags`

    [[nodiscard]] static constexpr std::size_t message_capacity() noexcept {
        return MessageCapacity;
    }

    /// Copy the payload into the inline buffer, truncating (and flagging) when
    /// it does not fit. Bounded memcpy, no allocation, no exception path.
    void assign_message(std::string_view text) noexcept {
        const std::size_t copied = std::min(text.size(), MessageCapacity);
        if (copied > 0) {
            std::memcpy(message.data(), text.data(), copied);
        }
        message_length = static_cast<std::uint16_t>(copied);
        if (text.size() > MessageCapacity) {
            flags |= flag_truncated;
        }
    }

    /// Current payload as a view into the inline buffer (no copy, no heap).
    [[nodiscard]] std::string_view message_view() const noexcept {
        return std::string_view(message.data(), message_length);
    }

    [[nodiscard]] bool truncated() const noexcept {
        return (flags & flag_truncated) != 0U;
    }

    core::Timestamp stamp{};                          ///< monotonic nanoseconds
    std::uint32_t thread_id{};                        ///< cached SYS_gettid
    LogLevel level{LogLevel::Info};                   ///< severity
    std::uint8_t flags{};                             ///< bit0: truncated
    std::uint16_t message_length{};                   ///< bytes used of message
    const char* file_name{};                          ///< source_location::file_name
    const char* function_name{};                      ///< source_location::function_name
    std::uint32_t line{};                             ///< source_location::line
    /// 8-aligned so the payload starts at offset 40 (design D4 layout table).
    alignas(std::uint64_t) std::array<char, MessageCapacity> message{}; ///< inline payload
};

} // namespace nexigen::logging
