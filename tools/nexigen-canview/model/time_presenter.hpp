// nexigen-canview — hardware time, estimated UTC and delta presentation.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "anchor_table.hpp"
#include "can_view.hpp"
#include "delta_mode.hpp"
#include "timestamp_mode.hpp"

namespace nexigen::canview {

/// @brief One rendered time or delta cell.
struct TimeCell {
    std::array<char, 48> text{};   ///< NUL-terminated display text.
    std::array<char, 96> reason{}; ///< Why the value is unavailable.
    std::int64_t value{0};         ///< Parsed numeric value when available.
    bool available{false};         ///< Whether the value is defined.
    bool estimated{false};         ///< UTC derived from an anchor.
    bool negative{false};          ///< Signed value below zero.

    /// @brief Rendered text.
    /// @return The cell text.
    [[nodiscard]] std::string to_string() const { return std::string(text.data()); }
};

/// @brief Shared time state used by every view.
struct TimeContext {
    /// @brief Addressable channels.
    static constexpr std::size_t channels{32U};

    AnchorTable anchors{};                               ///< UTC anchor epochs.
    std::array<std::uint64_t, channels> session_start{}; ///< First hardware time per channel.
    std::array<bool, channels> session_started{};        ///< Whether a start exists.
    bool assume_shared_timebase{false};                  ///< Allow cross-channel deltas.

    /// @brief Record the first hardware time of a channel.
    /// @param record Record observed at ingest.
    void observe(const CanRecordView& record) noexcept;
    /// @brief Whether a channel has a session start.
    /// @param channel_id Channel identity.
    /// @return `true` after `observe()`.
    [[nodiscard]] bool has_session_start(std::uint16_t channel_id) const noexcept {
        return session_started[channel_id % channels];
    }
    /// @brief First hardware time of a channel.
    /// @param channel_id Channel identity.
    /// @return Start timestamp, or zero when unset.
    [[nodiscard]] std::uint64_t session_start_ns(std::uint16_t channel_id) const noexcept {
        return session_start[channel_id % channels];
    }
    /// @brief Reset session starts.
    void reset_session() noexcept;
};

/// @brief Cold-path time rendering shared by the model and the GUI.
class TimePresenter {
public:
    /// @brief Estimated UTC for one record.
    /// @param record Record to convert.
    /// @param context Anchor and time-base state.
    /// @return UTC nanoseconds, or `std::nullopt` when the epoch is unavailable.
    [[nodiscard]] static std::optional<std::int64_t> utc_ns(const CanRecordView& record,
                                                            const TimeContext& context) noexcept;

    /// @brief Anchor used by one record.
    /// @param record Record to inspect.
    /// @param context Anchor table.
    /// @return The anchor, or `std::nullopt` when evicted/unavailable.
    [[nodiscard]] static std::optional<AnchorEvent> anchor(const CanRecordView& record,
                                                           const TimeContext& context) noexcept;

    /// @brief Render the estimated-UTC column.
    /// @param record Record to render.
    /// @param context Anchor and time-base state.
    /// @return Cell with `UTC (est.)` text, or an unavailable cell.
    [[nodiscard]] static TimeCell utc(const CanRecordView& record,
                                      const TimeContext& context) noexcept;

    /// @brief Render the primary hardware-time column.
    /// @param record Record to render.
    /// @param mode Selected timestamp mode.
    /// @param view View owning T0.
    /// @param context Session and shared-time state.
    /// @return Rendered cell.
    [[nodiscard]] static TimeCell hardware(const CanRecordView& record, TimestampMode mode,
                                           const CanView& view,
                                           const TimeContext& context) noexcept;

    /// @brief Render the delta column.
    /// @param record Current record.
    /// @param previous Previous visible record, if any.
    /// @param mode Selected delta mode.
    /// @param view View owning T0.
    /// @param context Session and shared-time state.
    /// @return Rendered cell; unavailable when the basis crosses channels
    ///         without a shared timebase.
    [[nodiscard]] static TimeCell delta(const CanRecordView& record, const CanRecordView* previous,
                                        DeltaMode mode, const CanView& view,
                                        const TimeContext& context) noexcept;

    /// @brief Whether a delta exceeds the anomaly threshold.
    /// @param delta_ns Signed delta.
    /// @param threshold_ns Configured threshold; non-positive disables it.
    /// @return `true` when the magnitude exceeds the threshold.
    [[nodiscard]] static bool is_anomalous(std::int64_t delta_ns,
                                           std::int64_t threshold_ns) noexcept;
};

} // namespace nexigen::canview
