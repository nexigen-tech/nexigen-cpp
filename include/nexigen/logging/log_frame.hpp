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

/// @brief Fixed 256-byte log frame published across the process boundary (`REQ_LOGC_004`).
///
/// Layout per design D1: a 31-byte header plus `payload[225]`, so the frame is
/// exactly four cache lines. The type is pointer-free, trivially copyable, and
/// `alignas(64)`, which lets it be memcpy-ed into (and out of) iceoryx2 shared
/// memory without serialization. The publishing worker owns the fields while
/// building a frame; subscribers receive an immutable copy after publication.
///
/// @note Spec `logging/iceoryx2-logger`, requirement "Log frame is a fixed zero-copy frame".
struct alignas(64) LogFrame {
    /// @brief Monotonic OS timestamp of the record, in nanoseconds.
    std::uint64_t timestamp_ns{};
    /// @brief Four-byte ASCII application code (e.g. `"LOGD"`), not NUL-terminated.
    char app_id[4]{};
    /// @brief Reserved context identifier; stays `0` until contexts land (`LOGC_003`).
    std::uint32_t context_id{};
    /// @brief Cached `SYS_gettid` value of the producing thread.
    std::uint32_t thread_id{};
    /// @brief PID of the client process.
    std::uint32_t pid{};
    /// @brief Drop delta accumulated since the previous frame.
    std::uint32_t dropped_counter{};
    /// @brief Number of bytes of `payload` in use.
    std::uint16_t payload_size{};
    /// @brief Severity of the record (one byte, so the layout is pinned).
    core::LogLevel log_level{core::LogLevel::Info};
    /// @brief Rendered message text; not NUL-terminated.
    std::uint8_t payload[225]{};

    /// @brief The `app_id` as a non-NUL-terminated view, trimmed at the first NUL byte.
    ///
    /// @return View over the leading non-NUL bytes of `app_id`; empty when the code is
    ///         unset.
    /// @note Never throws and touches only frame-local storage.
    [[nodiscard]] std::string_view app_id_text() const noexcept {
        std::size_t n = 0;
        while (n < sizeof(app_id) && app_id[n] != '\0') {
            ++n;
        }
        return std::string_view(app_id, n);
    }

    /// @brief The rendered message as a view bounded by `payload_size`.
    ///
    /// @return View of the first `payload_size` bytes of `payload`, clamped to
    ///         `sizeof(payload)` so a hostile or corrupt size cannot read past the frame.
    /// @note Never throws and touches only frame-local storage.
    [[nodiscard]] std::string_view payload_text() const noexcept {
        const std::size_t n = payload_size < sizeof(payload) ? payload_size : sizeof(payload);
        return std::string_view(reinterpret_cast<const char*>(payload), n);
    }

    /// @brief Cold-path diagnostic rendering of the frame metadata and payload length.
    ///
    /// @return Text containing the timestamp, app code, context, thread, pid, drop delta,
    ///         level, and payload size (not the payload bytes).
    /// @note Project mandate 9 (`to_string` on every public value type).
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

/// @brief Free `to_string` entry point for a frame (mandate 9).
///
/// @param frame Frame to render.
/// @return Exactly `frame.to_string()`.
/// @note Project mandate 9; ADL-friendly mirror of the member function.
[[nodiscard]] inline std::string to_string(const LogFrame& frame) {
    return frame.to_string();
}

/// @brief Cold-path diagnostic JSON writer for a frame (mandate 10).
///
/// @param j Destination JSON object; overwritten with the frame fields.
/// @param frame Frame to serialize.
/// @note Project mandate 10; the body lives in `log_frame.cpp` so consumers do not
///       recompile it.
void to_json(nlohmann::json& j, const LogFrame& frame);
/// @brief Cold-path JSON reader performing a full round-trip.
///
/// Hostile input (wrong types, missing fields, oversized payload) leaves the
/// target unchanged instead of throwing.
///
/// @param j Source JSON object.
/// @param out Destination frame, updated field by field.
/// @note Project mandate 10; non-throwing under `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, LogFrame& out) noexcept;

} // namespace nexigen::logging

/// @brief `std::formatter` specialization: the frame formats as its diagnostic string.
///
/// Forwards to `std::formatter<std::string_view>` after materializing
/// `to_string()`, so every string format spec applies.
///
/// @note Project mandate 11 (`std::formatter` for every public value type).
template <> struct std::formatter<nexigen::logging::LogFrame> : std::formatter<std::string_view> {
    /// @brief Write the frame's diagnostic text into the format output.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param frame Frame to render.
    /// @param ctx Format context receiving the text.
    /// @return Iterator past the last written character, as returned by the string formatter.
    /// @note Allocates the temporary diagnostic string; cold path only.
    template <class Ctx> auto format(const nexigen::logging::LogFrame& frame, Ctx& ctx) const {
        const std::string text = frame.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
