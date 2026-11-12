// nexigen::metrics — fixed cross-process wire frame for one harvested metric
// (change add-metrics-daemon; spec metrics/iceoryx2-metric-sink "MetricFrame
// wire type"; design D1/D3).
//
// The sink fans a MetricBatchSnapshot out into one MetricFrame per record and
// publishes each over iceoryx2; the daemon receives the same POD. The frame is
// trivially copyable so it survives the zero-copy shared-memory exchange
// unchanged (com::concepts::Message). app_id mirrors LogFrame: a 4-byte ASCII
// code, not the 63-byte app name.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/metrics/metric_record.hpp>

namespace nexigen::metrics {

/// @brief One harvested record plus the publisher identity, published as a POD.
///
/// The sole cross-process contract between `Iceoryx2MetricSink` and the daemon:
/// the sink emits one frame per record and the daemon receives the same bytes
/// through zero-copy shared memory. Fixed-size and trivially copyable, so the
/// frame never carries pointers or count-dependent storage.
///
/// @note Spec `metrics/iceoryx2-metric-sink`, requirement "MetricFrame wire
///       type".
struct MetricFrame {
    char app_id[4]{};             ///< 4-byte ASCII code (e.g. "LIDR").
    std::uint32_t pid{0};         ///< Publisher PID (`::getpid()`, cached at start).
    std::int64_t timestamp_ns{0}; ///< Window stamp (`CLOCK_MONOTONIC_RAW`).
    MetricRecord record{};        ///< One harvested metric.

    /// @brief The `app_id` as a non-null-terminated view.
    ///
    /// @return View trimmed at the first NUL (at most four bytes).
    /// @note Spec `metrics/iceoryx2-metric-sink`, requirement "MetricFrame wire
    ///       type".
    [[nodiscard]] std::string_view app_id_text() const noexcept {
        std::size_t n = 0U;
        while (n < sizeof(app_id) && app_id[n] != '\0') {
            ++n;
        }
        return std::string_view(app_id, n);
    }

    /// @brief Render the frame as a cold-path diagnostic string.
    ///
    /// @return Text naming the application id, PID, timestamp, and record.
    /// @note Project mandate 9; diagnostic only, never called on the publish
    ///       hot path.
    [[nodiscard]] std::string to_string() const {
        std::string out = "MetricFrame(app=" + std::string(app_id_text());
        out += ", pid=" + std::to_string(pid);
        out += ", ts_ns=" + std::to_string(timestamp_ns);
        out += ", record=" + record.to_string() + ")";
        return out;
    }
};

static_assert(std::is_trivially_copyable_v<MetricFrame>,
              "MetricFrame must survive the zero-copy shared-memory exchange");
static_assert(offsetof(MetricFrame, record) % alignof(MetricRecord) == 0U,
              "the record must be naturally aligned inside the frame");

/// @brief Free diagnostic helper mirroring `MetricFrame::to_string()`.
///
/// @param frame Frame to render.
/// @return The member's text.
/// @note Project mandate 9; cold path only.
[[nodiscard]] inline std::string to_string(const MetricFrame& frame) {
    return frame.to_string();
}

/// @brief Write a frame as JSON (cold path).
///
/// @param j Output JSON object with the app id, PID, timestamp, and record.
/// @param frame Frame to serialize.
/// @note Spec `metrics/iceoryx2-metric-sink`, requirement "MetricFrame wire
///       type"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const MetricFrame& frame);
/// @brief Parse a frame from JSON without throwing.
///
/// Values are assembled into a default-constructed frame first, so fields that
/// are missing or wrongly typed keep their defaults; the app id is truncated to
/// the four-byte buffer and the embedded `MetricRecord` is parsed with its own
/// rules.
///
/// @param j JSON object to read.
/// @param out Destination, replaced by the assembled frame.
/// @note Spec `metrics/iceoryx2-metric-sink`, requirement "MetricFrame wire
///       type".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, MetricFrame& out) noexcept;

} // namespace nexigen::metrics

/// @brief `std::formatter` for `MetricFrame`, rendering its diagnostic string.
///
/// @note Project mandate 11; formatting is cold path only.
template <>
struct std::formatter<nexigen::metrics::MetricFrame> : std::formatter<std::string_view> {
    /// @brief Format the frame through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param frame Frame to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::metrics::MetricFrame& frame, Ctx& ctx) const {
        const std::string text = frame.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
