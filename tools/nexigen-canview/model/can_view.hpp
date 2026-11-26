// nexigen-canview — one independent view (change add-can-monitor-view).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

#include "can_record_view.hpp"
#include "delta_mode.hpp"
#include "filter_criteria.hpp"
#include "find_state.hpp"
#include "record_source.hpp"
#include "slot_bitset.hpp"
#include "snapshot_source.hpp"
#include "time_column_mode.hpp"
#include "timestamp_mode.hpp"
#include "view_kind.hpp"
#include "visible_index.hpp"

namespace nexigen::canview {

/// @brief Table presentation of one view.
enum class TableMode : std::uint8_t {
    Trace,     ///< Chronological records.
    Overwrite, ///< One row per `(channel, id, extended, kind)`.
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `TableMode`.
template <> struct EnumTraits<nexigen::canview::TableMode> {
    /// @brief One value/name row.
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::TableMode value; ///< Enumerator. ///< Enumerator.
        std::string_view name;             ///< Canonical name.
    };
    /// @brief Canonical table.
    /// @brief Canonical table.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::canview::TableMode::Trace, "Trace"},
        {nexigen::canview::TableMode::Overwrite, "Overwrite"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical name of a table mode.
[[nodiscard]] constexpr std::string_view to_string(TableMode mode) noexcept {
    return core::enum_name(mode);
}

/// @brief Parse a canonical table mode.
[[nodiscard]] constexpr bool from_string(std::string_view name, TableMode& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a table mode as JSON.
inline void to_json(nlohmann::json& j, const TableMode mode) {
    core::mandate::enum_to_json(j, mode);
}

/// @brief Read a table mode from JSON without throwing.
inline void from_json(const nlohmann::json& j, TableMode& mode) noexcept {
    core::mandate::enum_from_json(j, mode);
}

/// @brief Default settings applied to a new view.
struct ViewDefaults {
    TableMode table_mode{TableMode::Trace};              ///< Table presentation.
    TimestampMode timestamp_mode{TimestampMode::Driver}; ///< Primary time.
    DeltaMode delta_mode{DeltaMode::PreviousVisible};    ///< Delta basis.
    TimeColumnMode time_columns{TimeColumnMode::HwOnly}; ///< Optional UTC column.
    bool auto_scroll{true};                              ///< Follow new records.
    std::int64_t anomaly_threshold_ns{0};                ///< Period outlier threshold.
};

/// @brief Which state groups a clone copies.
struct CloneOptions {
    bool filter{true};     ///< Copy filter criteria.
    bool table_time{true}; ///< Copy table mode and time settings.
    bool selection{true};  ///< Copy selection.
    bool bookmarks{true};  ///< Copy bookmarks.
    bool t0{true};         ///< Copy T0 when inside the snapshot.
};

/// @brief Clipping adjustments applied to a clone.
struct CloneAdjustments {
    std::uint64_t selection_dropped{0}; ///< Selected records outside the snapshot.
    std::uint64_t bookmarks_dropped{0}; ///< Bookmarked records outside the snapshot.
    bool t0_cleared{false};             ///< T0 was outside the snapshot.

    /// @brief Whether anything was clipped.
    /// @return `true` when at least one group was adjusted.
    [[nodiscard]] bool any() const noexcept {
        return selection_dropped > 0U || bookmarks_dropped > 0U || t0_cleared;
    }
    /// @brief Cold-path status text.
    /// @return Human-readable clipping summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Bounded density overview of one view's visible records.
struct Minimap {
    static constexpr std::size_t buckets{256U};   ///< Fixed bucket count.
    std::array<std::uint32_t, buckets> density{}; ///< Records per bucket.
    std::size_t visible{0};                       ///< Visible records at rebuild.

    /// @brief Clear the density and visible count.
    void clear() noexcept {
        density.fill(0U);
        visible = 0U;
    }
};

/// @brief Independent filter, visible index, selection, find and time state.
///
/// A LIVE view borrows the shared ring and follows new records. A PAUSED view
/// owns an immutable `SnapshotSource`. Everything else is per view.
class CanView {
public:
    CanView() = default;
    CanView(const CanView&) = delete;
    CanView& operator=(const CanView&) = delete;

    /// @brief Initialise a LIVE view.
    /// @param id Stable identity.
    /// @param kind Scope kind.
    /// @param channel_id Fixed channel for `Channel`/`Node`, ignored for `All`.
    /// @param node_id Fixed node for `Node`, ignored otherwise.
    /// @param node_slot Resolved CANopen registry slot, if any.
    /// @param title Display title.
    /// @param source Shared live history.
    /// @param slot_count Power-of-two slot count of the history.
    /// @param defaults Initial table/time settings.
    void init_live(std::uint32_t id, ViewKind kind, std::uint16_t channel_id, std::uint8_t node_id,
                   std::optional<std::uint16_t> node_slot, std::string title,
                   const RecordSource& source, std::size_t slot_count,
                   const ViewDefaults& defaults);

    /// @brief Initialise a PAUSED view over an owned snapshot.
    /// @param id Stable identity.
    /// @param kind Scope kind copied from the source.
    /// @param channel_id Fixed channel.
    /// @param node_id Fixed node.
    /// @param node_slot Resolved CANopen registry slot.
    /// @param title Display title.
    /// @param snapshot Owned immutable history.
    /// @param slot_count Slot count used for selection bit keys.
    /// @param defaults Initial table/time settings.
    void init_paused(std::uint32_t id, ViewKind kind, std::uint16_t channel_id,
                     std::uint8_t node_id, std::optional<std::uint16_t> node_slot,
                     std::string title, std::unique_ptr<SnapshotSource> snapshot,
                     std::size_t slot_count, const ViewDefaults& defaults);

    /// @brief Copy filter, table/time and selected state groups from a source.
    /// @param other Source view.
    /// @param options Groups to copy.
    /// @return Clipping adjustments.
    [[nodiscard]] CloneAdjustments copy_state_from(const CanView& other,
                                                   const CloneOptions& options) noexcept;

    /// @brief Stable identity.
    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }
    /// @brief Scope kind.
    [[nodiscard]] ViewKind kind() const noexcept { return kind_; }
    /// @brief Fixed channel identity.
    [[nodiscard]] std::uint16_t channel_id() const noexcept { return channel_id_; }
    /// @brief Fixed node identity.
    [[nodiscard]] std::uint8_t node_id() const noexcept { return node_id_; }
    /// @brief Resolved CANopen slot.
    [[nodiscard]] std::optional<std::uint16_t> node_slot() const noexcept { return node_slot_; }
    /// @brief Display title.
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    /// @brief Replace the display title.
    /// @param title New title; the caller keeps it non-empty.
    void set_title(std::string title) noexcept { title_ = std::move(title); }
    /// @brief Whether the view follows live history.
    [[nodiscard]] bool live() const noexcept { return live_; }
    /// @brief Whether a node view lost its binding.
    [[nodiscard]] bool orphaned() const noexcept { return orphaned_; }
    /// @brief Mark the node binding state.
    /// @param slot Resolved slot or `std::nullopt` when unbound.
    void set_node_binding(std::optional<std::uint16_t> slot) noexcept;

    /// @brief Filter criteria.
    [[nodiscard]] FilterCriteria& filter() noexcept { return index_.criteria(); }
    /// @brief Filter criteria.
    [[nodiscard]] const FilterCriteria& filter() const noexcept { return index_.criteria(); }
    /// @brief Visible index.
    [[nodiscard]] VisibleIndex& index() noexcept { return index_; }
    /// @brief Visible index.
    [[nodiscard]] const VisibleIndex& index() const noexcept { return index_; }
    /// @brief Find state.
    [[nodiscard]] FindState& find() noexcept { return find_; }
    /// @brief Find state.
    [[nodiscard]] const FindState& find() const noexcept { return find_; }

    /// @brief History source (owned snapshot or borrowed ring).
    [[nodiscard]] const RecordSource& source() const noexcept { return *source_; }
    /// @brief Channel alias used by the textual filter.
    [[nodiscard]] std::string_view channel_alias() const noexcept { return channel_alias_; }
    /// @brief Node alias used by the textual filter.
    [[nodiscard]] std::string_view node_alias() const noexcept { return node_alias_; }
    /// @brief Set the aliases used by the textual filter.
    /// @param channel New channel alias.
    /// @param node New node alias.
    void set_aliases(std::string channel, std::string node);
    /// @brief Labels passed to filter and find.
    /// @return Alias pair.
    [[nodiscard]] FilterLabels labels() const noexcept {
        return FilterLabels{channel_alias_, node_alias_};
    }

    /// @brief Start a filter rebuild.
    void begin_rebuild() noexcept { index_.begin_rebuild(*source_); }
    /// @brief Scan one bounded rebuild chunk.
    /// @param max_records Chunk size.
    /// @return Progress after the chunk.
    VisibleIndex::Progress step_rebuild(std::size_t max_records) {
        return index_.step_rebuild(*source_, max_records, labels());
    }
    /// @brief Rebuild the visible index completely.
    void rebuild_all();
    /// @brief Rebuild the bounded minimap overview.
    void rebuild_minimap();
    /// @brief Last built minimap overview.
    [[nodiscard]] const Minimap& minimap() const noexcept { return minimap_; }
    /// @brief Append a new record when visible.
    /// @param seq Absolute record number.
    /// @return `true` when appended.
    bool append_if_visible(std::uint64_t seq) {
        return live_ && index_.append_if_visible(*source_, seq, labels());
    }

    /// @brief Number of visible entries in the displayed range.
    /// @return Count.
    [[nodiscard]] std::size_t display_count() const noexcept;
    /// @brief Absolute number at one displayed order index.
    /// @param order Order index.
    /// @return Record number, or zero.
    [[nodiscard]] std::uint64_t display_at(std::size_t order) const noexcept;
    /// @brief Oldest displayed sequence.
    [[nodiscard]] std::uint64_t display_floor() const noexcept;
    /// @brief One past the newest displayed sequence.
    [[nodiscard]] std::uint64_t display_end() const noexcept;

    /// @brief Table presentation.
    [[nodiscard]] TableMode table_mode() const noexcept { return table_mode_; }
    /// @brief Change the table presentation.
    /// @param mode New mode.
    void set_table_mode(TableMode mode) noexcept { table_mode_ = mode; }

    /// @brief Maximum stored column widths per table mode.
    static constexpr std::size_t max_table_columns{16U};

    /// @brief Stored column widths for one table mode.
    /// @param mode Table mode.
    /// @return Borrowed width span; empty when no layout was captured yet.
    [[nodiscard]] std::span<const float> table_widths(TableMode mode) const noexcept {
        return mode == TableMode::Trace
                   ? std::span<const float>{trace_widths_.data(), trace_width_count_}
                   : std::span<const float>{overwrite_widths_.data(), overwrite_width_count_};
    }

    /// @brief Store the current column widths for one table mode.
    /// @param mode Table mode.
    /// @param widths Widths in visual column order.
    void set_table_widths(TableMode mode, std::span<const float> widths) noexcept {
        std::array<float, max_table_columns>& target =
            mode == TableMode::Trace ? trace_widths_ : overwrite_widths_;
        std::size_t& count = mode == TableMode::Trace ? trace_width_count_ : overwrite_width_count_;
        count = widths.size() < target.size() ? widths.size() : target.size();
        for (std::size_t index = 0; index < count; ++index) {
            target[index] = widths[index];
        }
    }

    /// @brief Ask the next table draw to apply the stored column widths.
    void request_table_layout() noexcept { table_layout_pending_ = true; }
    /// @brief Consume a pending layout application request.
    /// @return `true` once, after a clone copied a table layout.
    [[nodiscard]] bool consume_table_layout_request() noexcept {
        const bool pending = table_layout_pending_;
        table_layout_pending_ = false;
        return pending;
    }
    /// @brief Whether this view copied a table layout from a clone source.
    /// @return `true` when ImGui saved table settings must not override the copy.
    [[nodiscard]] bool table_layout_copied() const noexcept { return table_layout_copied_; }

    /// @brief Primary time mode.
    [[nodiscard]] TimestampMode timestamp_mode() const noexcept { return timestamp_mode_; }
    /// @brief Change the primary time mode.
    void set_timestamp_mode(TimestampMode mode) noexcept { timestamp_mode_ = mode; }
    /// @brief Delta mode.
    [[nodiscard]] DeltaMode delta_mode() const noexcept { return delta_mode_; }
    /// @brief Change the delta mode.
    void set_delta_mode(DeltaMode mode) noexcept { delta_mode_ = mode; }
    /// @brief Time column mode.
    [[nodiscard]] TimeColumnMode time_columns() const noexcept { return time_columns_; }
    /// @brief Change the time column mode.
    void set_time_columns(TimeColumnMode mode) noexcept { time_columns_ = mode; }
    /// @brief Period outlier threshold.
    [[nodiscard]] std::int64_t anomaly_threshold_ns() const noexcept {
        return anomaly_threshold_ns_;
    }
    /// @brief Change the period outlier threshold.
    /// @param value New threshold in nanoseconds.
    void set_anomaly_threshold_ns(std::int64_t value) noexcept { anomaly_threshold_ns_ = value; }

    /// @brief Whether auto-scroll follows new records.
    [[nodiscard]] bool auto_scroll() const noexcept { return auto_scroll_; }
    /// @brief Change auto-scroll.
    /// @param value New state.
    void set_auto_scroll(bool value) noexcept { auto_scroll_ = value; }
    /// @brief Scroll position in displayed order.
    [[nodiscard]] std::size_t scroll() const noexcept { return scroll_; }
    /// @brief Change the scroll position.
    /// @param value New position.
    void set_scroll(std::size_t value) noexcept { scroll_ = value; }

    /// @brief Pause and scroll to one record (heatmap click).
    /// @param seq Absolute record number.
    /// @return `true` when the record is visible and focused.
    bool focus_record(std::uint64_t seq) noexcept;

    /// @brief Pause or resume a LIVE view's displayed range.
    /// @param paused New state.
    void set_paused(bool paused) noexcept;
    /// @brief Whether a LIVE view is paused.
    [[nodiscard]] bool paused() const noexcept { return paused_; }

    /// @brief T0 absolute sequence.
    [[nodiscard]] std::optional<std::uint64_t> t0_seq() const noexcept { return t0_seq_; }
    /// @brief T0 hardware timestamp.
    [[nodiscard]] std::int64_t t0_ns() const noexcept { return t0_ns_; }
    /// @brief T0 channel.
    [[nodiscard]] std::uint16_t t0_channel() const noexcept { return t0_channel_; }
    /// @brief Whether T0 is set.
    [[nodiscard]] bool has_t0() const noexcept { return t0_seq_.has_value(); }
    /// @brief Set T0 from one record.
    /// @param record Record to bind.
    void set_t0(const CanRecordView& record) noexcept;
    /// @brief Clear T0.
    void clear_t0() noexcept { t0_seq_.reset(); }

    /// @brief Select only one record.
    /// @param seq Absolute record number.
    void select_only(std::uint64_t seq) noexcept;
    /// @brief Toggle one record's selection.
    /// @param seq Absolute record number.
    void toggle_selection(std::uint64_t seq) noexcept;
    /// @brief Select every displayed record.
    void select_all_displayed() noexcept;
    /// @brief Clear selection.
    void clear_selection() noexcept;
    /// @brief Whether a record is selected.
    /// @param seq Absolute record number.
    /// @return `true` when selected.
    [[nodiscard]] bool is_selected(std::uint64_t seq) const noexcept;
    /// @brief Number of selected slots.
    [[nodiscard]] std::uint64_t selected_count() const noexcept { return selection_.count(); }
    /// @brief Last clicked record.
    /// @return Absolute number or `std::nullopt`.
    [[nodiscard]] std::optional<std::uint64_t> last_selected() const noexcept {
        return last_selected_;
    }

    /// @brief Toggle a bookmark.
    /// @param seq Absolute record number.
    /// @return New bookmark state.
    bool toggle_bookmark(std::uint64_t seq) noexcept;
    /// @brief Whether a record is bookmarked.
    /// @param seq Absolute record number.
    /// @return `true` when bookmarked.
    [[nodiscard]] bool is_bookmarked(std::uint64_t seq) const noexcept;
    /// @brief Number of bookmarked slots.
    [[nodiscard]] std::uint64_t bookmarked_count() const noexcept { return bookmarks_.count(); }

    /// @brief Mark one record for export.
    /// @param seq Absolute record number.
    void mark_export(std::uint64_t seq) noexcept;
    /// @brief Whether a record is marked for export.
    /// @param seq Absolute record number.
    /// @return `true` when marked.
    [[nodiscard]] bool is_export_marked(std::uint64_t seq) const noexcept;
    /// @brief Clear the export selection.
    void clear_export() noexcept { export_selection_.clear(); }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with scope and counts.
    [[nodiscard]] std::string to_string() const;

private:
    [[nodiscard]] std::size_t slot_of(std::uint64_t seq) const noexcept { return seq & slot_mask_; }

    std::uint32_t id_{0};                      ///< Stable identity.
    ViewKind kind_{ViewKind::All};             ///< Scope kind.
    std::uint16_t channel_id_{0};              ///< Fixed channel.
    std::uint8_t node_id_{0};                  ///< Fixed node.
    std::optional<std::uint16_t> node_slot_{}; ///< Resolved CANopen slot.
    std::string title_{};                      ///< Display title.
    std::string channel_alias_{};              ///< Channel alias for text matching.
    std::string node_alias_{};                 ///< Node alias for text matching.
    bool live_{true};                          ///< Follows live history.
    bool orphaned_{false};                     ///< Node binding missing.
    bool paused_{false};                       ///< LIVE view paused.
    std::uint64_t pause_tail_{0};              ///< Displayed end while paused.

    const RecordSource* source_{nullptr};        ///< Borrowed or owned source.
    std::unique_ptr<SnapshotSource> snapshot_{}; ///< Owned PAUSED history.
    VisibleIndex index_{};                       ///< Visible records.
    FindState find_{};                           ///< Find results.
    SlotBitset selection_{};                     ///< Selected slots.
    SlotBitset bookmarks_{};                     ///< Bookmarked slots.
    SlotBitset export_selection_{};              ///< Export selection.
    Minimap minimap_{};                          ///< Bounded view overview.
    std::size_t slot_mask_{0};                   ///< Power-of-two slot mask.

    TableMode table_mode_{TableMode::Trace};                  ///< Table presentation.
    TimestampMode timestamp_mode_{TimestampMode::Driver};     ///< Primary time.
    DeltaMode delta_mode_{DeltaMode::PreviousVisible};        ///< Delta basis.
    TimeColumnMode time_columns_{TimeColumnMode::HwOnly};     ///< Time columns.
    std::int64_t anomaly_threshold_ns_{0};                    ///< Outlier threshold.
    bool auto_scroll_{true};                                  ///< Follow new records.
    std::size_t scroll_{0};                                   ///< Displayed-order scroll.
    std::array<float, max_table_columns> trace_widths_{};     ///< Trace column widths.
    std::size_t trace_width_count_{0};                        ///< Valid trace widths.
    std::array<float, max_table_columns> overwrite_widths_{}; ///< Overwrite column widths.
    std::size_t overwrite_width_count_{0};                    ///< Valid overwrite widths.
    bool table_layout_pending_{false};                        ///< Apply copied widths on next draw.
    bool table_layout_copied_{false}; ///< Clone owns copied layout, not ImGui ini.

    std::optional<std::uint64_t> last_selected_{}; ///< Last clicked record.
    std::optional<std::uint64_t> t0_seq_{};        ///< T0 record.
    std::int64_t t0_ns_{0};                        ///< T0 hardware time.
    std::uint16_t t0_channel_{0};                  ///< T0 channel.
};

} // namespace nexigen::canview
