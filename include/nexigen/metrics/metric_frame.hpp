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

/// One harvested record plus the publisher identity, published as a fixed POD.
struct MetricFrame {
    char app_id[4]{};             ///< 4-byte ASCII code (e.g. "LIDR")
    std::uint32_t pid{0};         ///< publisher PID (::getpid(), cached at start)
    std::int64_t timestamp_ns{0}; ///< window stamp (CLOCK_MONOTONIC_RAW)
    MetricRecord record{};        ///< one harvested metric

    /// The app_id as a non-null-terminated view (trimmed at the first NUL).
    [[nodiscard]] std::string_view app_id_text() const noexcept {
        std::size_t n = 0U;
        while (n < sizeof(app_id) && app_id[n] != '\0') {
            ++n;
        }
        return std::string_view(app_id, n);
    }

    /// Cold-path diagnostic (mandate 9).
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

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const MetricFrame& frame) {
    return frame.to_string();
}

/// Cold-path JSON (mandate 10), hostile-safe under JSON_NOEXCEPTION=1.
inline void to_json(nlohmann::json& j, const MetricFrame& frame) {
    j = nlohmann::json{
        {"app_id", std::string(frame.app_id_text())},
        {"pid", frame.pid},
        {"timestamp_ns", frame.timestamp_ns},
        {"record", frame.record},
    };
}

inline void from_json(const nlohmann::json& j, MetricFrame& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    MetricFrame parsed{};
    const auto app_it = j.find("app_id");
    if (app_it != j.end() && app_it->is_string()) {
        const std::string app = app_it->get<std::string>();
        const std::size_t take =
            app.size() < sizeof(parsed.app_id) ? app.size() : sizeof(parsed.app_id);
        for (std::size_t i = 0; i < take; ++i) {
            parsed.app_id[i] = app[i];
        }
    }
    const auto pid_it = j.find("pid");
    if (pid_it != j.end() && pid_it->is_number_unsigned()) {
        parsed.pid = pid_it->get<std::uint32_t>();
    }
    const auto ts_it = j.find("timestamp_ns");
    if (ts_it != j.end() && ts_it->is_number_integer()) {
        parsed.timestamp_ns = ts_it->get<std::int64_t>();
    }
    const auto rec_it = j.find("record");
    if (rec_it != j.end()) {
        from_json(*rec_it, parsed.record);
    }
    out = parsed;
}

} // namespace nexigen::metrics

/// The frame formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::MetricFrame> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::metrics::MetricFrame& frame, Ctx& ctx) const {
        const std::string text = frame.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
