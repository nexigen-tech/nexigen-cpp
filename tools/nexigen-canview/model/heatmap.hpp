// nexigen-canview — bounded bus-load heatmap.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

#include "bit_timing.hpp"
#include "record_source.hpp"
#include "time_presenter.hpp"

namespace nexigen::canview {

/// @brief Time axis of the heatmap.
enum class HeatmapAxis : std::uint8_t {
    Utc,            ///< Estimated UTC.
    HardwareLanes,  ///< Per-channel hardware lanes.
    SharedHardware, ///< One shared hardware axis.
};

/// @brief Aggregate of one channel inside one heatmap column.
struct HeatmapCell {
    std::uint64_t bit_time_ns{0}; ///< Estimated frame occupancy.
    std::uint64_t idle_ns{0};     ///< Measured bus-idle time.
    std::uint32_t errors{0};      ///< Error frames.
    std::uint32_t overloads{0};   ///< Overload frames.
    bool frames{false};           ///< Any frame in the bucket.
    bool measured{false};         ///< BusIdle data present.
    bool missing_bitrate{false};  ///< A frame had no configured bitrate.
};

/// @brief Aggregate of one CAN-ID bin in one column.
struct HeatmapIdBin {
    std::uint64_t bit_time_ns{0}; ///< Estimated occupancy.
    std::uint32_t frames{0};      ///< Frame count.
};

/// @brief One heatmap column.
struct HeatmapColumn {
    std::uint64_t first_seq{0};             ///< First record in the bucket, for click-to-record.
    std::array<HeatmapCell, 32> channels{}; ///< Per-channel aggregates.
    std::array<HeatmapIdBin, 9> bins{};     ///< Per-ID-bin aggregates.
};

/// @brief Rendered load of one cell.
struct HeatmapLoad {
    double measured{0.0};            ///< `1 - idle/bucket`, when measured.
    double estimated{0.0};           ///< `bit_time/bucket`, when a bitrate is known.
    bool measured_available{false};  ///< Measured load is available.
    bool estimated_available{false}; ///< Estimated load is available.
    bool missing_bitrate{false};     ///< A frame had no configured bitrate.
};

/// @brief Bounded, rebuildable heatmap cache.
class HeatmapWindow {
public:
    /// @brief Maximum rendered columns.
    static constexpr std::size_t max_columns{2048U};
    /// @brief Addressable channels.
    static constexpr std::size_t channels{32U};
    /// @brief ID bins for the selected channel.
    static constexpr std::size_t id_bins{9U};
    /// @brief Smallest step.
    static constexpr std::uint64_t min_step_ns{1'000'000ULL};

    /// @brief Reset the cache and step.
    /// @param max_columns Column cap; clamped to `max_columns`.
    void init(std::size_t max_columns_value = max_columns) noexcept;

    /// @brief Rebuild the visible range.
    /// @param source History to scan.
    /// @param context Anchor/time state for the UTC axis, or `nullptr`.
    /// @param axis Time axis.
    /// @param range_start Visible range start in hardware time.
    /// @param range_end Visible range end in hardware time.
    /// @param selected_channel Channel used for the ID-bin mode.
    /// @param timings Per-channel bitrate configuration.
    void rebuild(const RecordSource& source, const TimeContext* context, HeatmapAxis axis,
                 std::uint64_t range_start, std::uint64_t range_end, std::uint16_t selected_channel,
                 const std::array<ChannelTiming, channels>& timings);

    /// @brief Number of built columns.
    /// @return Column count.
    [[nodiscard]] std::size_t column_count() const noexcept { return columns_.size(); }
    /// @brief Effective step after the 2 048-column cap.
    /// @return Step in nanoseconds.
    [[nodiscard]] std::uint64_t effective_step_ns() const noexcept { return effective_step_ns_; }
    /// @brief Time axis.
    [[nodiscard]] HeatmapAxis axis() const noexcept { return axis_; }
    /// @brief Change the axis; the next rebuild uses it.
    /// @param axis New axis.
    void set_axis(HeatmapAxis axis) noexcept { axis_ = axis; }
    /// @brief Whether live rebuilds follow the newest history.
    [[nodiscard]] bool follow() const noexcept { return follow_; }
    /// @brief Change follow mode.
    /// @param follow New state.
    void set_follow(bool follow) noexcept { follow_ = follow; }
    /// @brief Increase the requested step (coarser).
    void step_coarser() noexcept;
    /// @brief Decrease the requested step (finer), bounded by `min_step_ns`.
    void step_finer() noexcept;
    /// @brief Requested step before the column cap.
    [[nodiscard]] std::uint64_t requested_step_ns() const noexcept { return requested_step_ns_; }

    /// @brief One built column.
    /// @param index Column index.
    /// @return Column or `nullptr`.
    [[nodiscard]] const HeatmapColumn* column(std::size_t index) const noexcept;
    /// @brief First record in one column.
    /// @param column Column index.
    /// @return Record number or `std::nullopt`.
    [[nodiscard]] std::optional<std::uint64_t> click_record(std::size_t column) const noexcept;
    /// @brief Rendered load of one cell.
    /// @param column Column index.
    /// @param channel_id Channel identity.
    /// @return Load values.
    [[nodiscard]] HeatmapLoad load(std::size_t column, std::uint16_t channel_id) const noexcept;

    /// @brief Inject an allocation failure (tests only).
    /// @param fail New state.
    void fail_allocation_for_tests(bool fail) noexcept { fail_allocation_ = fail; }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with column/step/axis state.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Ensure `columns_` covers an index.
    void ensure_column(std::size_t index);
    /// @brief Resolve the column of one record on the selected axis.
    [[nodiscard]] std::optional<std::size_t>
    column_for(const CanRecordView& record, std::size_t channel, HeatmapAxis axis,
               const TimeContext* context, std::uint64_t range_start, std::uint64_t range_end,
               std::uint64_t step, std::array<std::uint64_t, channels>& channel_base,
               std::array<bool, channels>& channel_started,
               std::optional<std::uint64_t>& utc_base) const noexcept;
    /// @brief Fold one record into a column.
    void accumulate(const CanRecordView& record, HeatmapColumn& bucket,
                    const std::array<ChannelTiming, channels>& timings,
                    std::uint16_t selected_channel) const noexcept;

    std::vector<HeatmapColumn> columns_{};           ///< Built columns.
    std::uint64_t requested_step_ns_{10'000'000ULL}; ///< Requested step.
    std::uint64_t effective_step_ns_{10'000'000ULL}; ///< Capped step.
    HeatmapAxis axis_{HeatmapAxis::SharedHardware};  ///< Time axis.
    std::uint16_t selected_channel_{0};              ///< ID-bin channel.
    bool follow_{true};                              ///< Follow the newest history.
    bool fail_allocation_{false};                    ///< Test-only allocation failure.
    std::size_t column_cap_{max_columns};            ///< Effective column cap.
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `HeatmapAxis`.
template <> struct EnumTraits<nexigen::canview::HeatmapAxis> {
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::HeatmapAxis value; ///< Enumerator.
        std::string_view name;               ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::canview::HeatmapAxis::Utc, "Utc"},
        {nexigen::canview::HeatmapAxis::HardwareLanes, "HardwareLanes"},
        {nexigen::canview::HeatmapAxis::SharedHardware, "SharedHardware"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical name of a heatmap axis.
[[nodiscard]] constexpr std::string_view to_string(HeatmapAxis value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse a heatmap axis.
[[nodiscard]] constexpr bool from_string(std::string_view name, HeatmapAxis& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write a heatmap axis as JSON.
inline void to_json(nlohmann::json& j, const HeatmapAxis value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read a heatmap axis without throwing.
inline void from_json(const nlohmann::json& j, HeatmapAxis& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canview
