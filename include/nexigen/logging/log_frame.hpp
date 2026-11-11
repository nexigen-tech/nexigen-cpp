// nexigen::logging — the fixed zero-copy log frame published over iceoryx2
// (change add-iceoryx2-logger; spec logging/iceoryx2-logger; design D1).
//
// LogFrame is the payload that crosses the process boundary between the
// Iceoryx2Logger worker and the future LogDaemon. It is trivially copyable,
// cache-line aligned, and pointer-free so it survives a zero-copy shared-memory
// exchange unchanged. The worker renders the message text into `payload`;
// final line formatting (date/PID/context name) is the daemon's job (design
// D5/D9).
#pragma once

#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/core/log_level.hpp>

namespace nexigen::logging {

/// Fixed 256-byte log frame (REQ_LOGC_004), layout per design D1:
/// header 31 bytes + payload[225] = 256 = 4 cache lines.
struct alignas(64) LogFrame {
    std::uint64_t timestamp_ns{};   ///< monotonic OS time of the record
    char app_id[4]{};               ///< 4-byte ASCII code (e.g. "LOGD")
    std::uint32_t context_id{};     ///< reserved = 0 until contexts land (LOGC_003)
    std::uint32_t thread_id{};      ///< cached SYS_gettid
    std::uint32_t pid{};            ///< client process PID
    std::uint32_t dropped_counter{};///< drop delta since the previous frame
    std::uint16_t payload_size{};   ///< bytes of `payload` in use
    core::LogLevel log_level{core::LogLevel::Info}; ///< severity (1 byte)
    std::uint8_t payload[225]{};    ///< rendered message text

    /// The app_id as a non-null-terminated view (trimmed at the first NUL).
    [[nodiscard]] std::string_view app_id_text() const noexcept {
        std::size_t n = 0;
        while (n < sizeof(app_id) && app_id[n] != '\0') {
            ++n;
        }
        return std::string_view(app_id, n);
    }

    /// The rendered message as a view bounded by payload_size.
    [[nodiscard]] std::string_view payload_text() const noexcept {
        const std::size_t n = payload_size < sizeof(payload) ? payload_size : sizeof(payload);
        return std::string_view(reinterpret_cast<const char*>(payload), n);
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        std::string out = "LogFrame(ts=" + std::to_string(timestamp_ns);
        out += ", app=" + std::string(app_id_text());
        out += ", ctx=" + std::to_string(context_id);
        out += ", tid=" + std::to_string(thread_id);
        out += ", pid=" + std::to_string(pid);
        out += ", dropped=" + std::to_string(dropped_counter);
        out += ", level=" + std::string(core::to_string(log_level));
        out += ", payload=" + std::to_string(payload_size) + ")";
        return out;
    }
};

// Pinned layout (design D1): the frame is exactly 4 cache lines, pointer-free.
static_assert(sizeof(LogFrame) == 256, "LogFrame must be exactly 256 bytes");
static_assert(alignof(LogFrame) == 64, "LogFrame must be cache-line aligned");
static_assert(std::is_trivially_copyable_v<LogFrame>, "LogFrame must be trivially copyable");
static_assert(sizeof(core::LogLevel) == 1, "LogLevel must occupy one byte in the frame");

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const LogFrame& frame) {
    return frame.to_string();
}

/// Cold-path diagnostic JSON (mandate 10): full round-trip under
/// JSON_NOEXCEPTION=1; hostile input leaves the target unchanged. Bodies live in
/// src/logging/log_frame.cpp so consumers do not recompile them.
void to_json(nlohmann::json& j, const LogFrame& frame);
void from_json(const nlohmann::json& j, LogFrame& out) noexcept;

} // namespace nexigen::logging

/// The frame formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::logging::LogFrame> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::logging::LogFrame& frame, Ctx& ctx) const {
        const std::string text = frame.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
