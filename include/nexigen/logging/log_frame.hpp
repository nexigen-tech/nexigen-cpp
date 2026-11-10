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
/// header 27 bytes + payload[229] = 256 = 4 cache lines.
struct alignas(64) LogFrame {
    std::uint64_t timestamp_ns{};   ///< monotonic OS time of the record
    char app_id[4]{};               ///< 4-byte ASCII code (e.g. "LOGD")
    std::uint32_t context_id{};     ///< reserved = 0 until contexts land (LOGC_003)
    std::uint32_t thread_id{};      ///< cached SYS_gettid
    std::uint32_t dropped_counter{};///< drop delta since the previous frame
    std::uint16_t payload_size{};   ///< bytes of `payload` in use
    core::LogLevel log_level{core::LogLevel::Info}; ///< severity (1 byte)
    std::uint8_t payload[229]{};    ///< rendered message text

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
/// JSON_NOEXCEPTION=1; hostile input leaves the target unchanged.
inline void to_json(nlohmann::json& j, const LogFrame& frame) {
    j = nlohmann::json{
        {"timestamp_ns", frame.timestamp_ns},
        {"app_id", std::string(frame.app_id_text())},
        {"context_id", frame.context_id},
        {"thread_id", frame.thread_id},
        {"dropped_counter", frame.dropped_counter},
        {"payload_size", frame.payload_size},
        {"log_level", frame.log_level},
        {"payload", std::string(frame.payload_text())},
    };
}

inline void from_json(const nlohmann::json& j, LogFrame& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    LogFrame parsed{};
    const auto read_u64 = [&j](const char* key, std::uint64_t& dst) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_unsigned()) {
            dst = it->get<std::uint64_t>();
        }
    };
    const auto read_u32 = [&j](const char* key, std::uint32_t& dst) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_unsigned()) {
            dst = it->get<std::uint32_t>();
        }
    };
    const auto read_u16 = [&j](const char* key, std::uint16_t& dst) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_unsigned()) {
            dst = it->get<std::uint16_t>();
        }
    };
    const auto read_str = [&j](const char* key, std::string& dst) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_string()) {
            dst = it->get<std::string>();
        }
    };
    read_u64("timestamp_ns", parsed.timestamp_ns);
    read_u32("context_id", parsed.context_id);
    read_u32("thread_id", parsed.thread_id);
    read_u32("dropped_counter", parsed.dropped_counter);
    read_u16("payload_size", parsed.payload_size);

    std::string app;
    read_str("app_id", app);
    const std::size_t app_take = app.size() < sizeof(parsed.app_id) ? app.size() : sizeof(parsed.app_id);
    for (std::size_t i = 0; i < app_take; ++i) {
        parsed.app_id[i] = app[i];
    }

    std::string payload;
    read_str("payload", payload);
    const std::size_t payload_take =
        payload.size() < sizeof(parsed.payload) ? payload.size() : sizeof(parsed.payload);
    for (std::size_t i = 0; i < payload_take; ++i) {
        parsed.payload[i] = static_cast<std::uint8_t>(payload[i]);
    }

    const auto lv = j.find("log_level");
    if (lv != j.end() && lv->is_string()) {
        core::LogLevel level = core::LogLevel::Info;
        if (core::from_string(lv->get<std::string>(), level)) {
            parsed.log_level = level;
        }
    }
    out = parsed;
}

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
