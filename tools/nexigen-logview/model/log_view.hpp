// nexigen-logview — independent presentation state over shared or frozen history.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "filter.hpp"
#include "find_state.hpp"
#include "history_source.hpp"
#include "delta_mode.hpp"
#include "minimap.hpp"
#include "record_ring.hpp"
#include "timestamp_mode.hpp"
#include "time_context.hpp"

namespace nexigen::logview {

/// @brief Stable identity of one log view.
using ViewId = std::uint64_t;

/// @brief Scope of a log view.
enum class ViewKind : std::uint8_t {
    All,   ///< Every source; uses the per-view source mask.
    Source ///< One fixed application code.
};

/// @brief Live views share the history ring; paused views own a snapshot.
enum class CloneMode : std::uint8_t {
    Live,  ///< Follow the shared live tail.
    Paused ///< Copy a bounded frozen snapshot.
};

/// @brief Maximum LIVE views including `All`.
inline constexpr std::size_t kMaxLiveViews = 8;
/// @brief Maximum PAUSED views.
inline constexpr std::size_t kMaxPausedViews = 16;

/// @brief Initial filters and presentation modes for a new view.
struct ViewDefaults {
    FilterCriteria criteria{};                             ///< Source, levels, and text filter.
    TimestampMode timestamp_mode{TimestampMode::Absolute}; ///< Timestamp mode.
    DeltaMode delta_mode{DeltaMode::PreviousVisible};      ///< Delta mode.
    std::uint64_t anomaly_threshold_ns{50'000'000};        ///< Anomaly threshold.
};

/// @brief One copy checkbox group in the clone dialog.
struct CloneOptions {
    bool levels{true};            ///< Copy level visibility.
    bool text{true};              ///< Copy the text filter.
    bool timestamp_mode{true};    ///< Copy the timestamp mode.
    bool delta_mode{true};        ///< Copy the delta mode.
    bool anomaly_threshold{true}; ///< Copy the anomaly threshold.
    bool selection{true};         ///< Copy the selection, clipped to the snapshot.
    bool bookmarks{true};         ///< Copy bookmarks, clipped to the snapshot.
    bool t0{true};                ///< Copy the T0 anchor when it is in range.
    bool find{true};              ///< Re-run the find query on the clone.
};

/// @brief State groups dropped while cloning into a smaller snapshot.
struct CloneAdjustments {
    std::size_t selection_dropped{0}; ///< Selected records absent from the clone.
    std::size_t bookmarks_dropped{0}; ///< Bookmarks absent from the clone.
    bool t0_cleared{false};           ///< T0 anchor outside the clone range.

    /// @brief Whether clipping changed any copied state.
    /// @return `true` when at least one group was adjusted.
    [[nodiscard]] bool any() const noexcept {
        return selection_dropped > 0U || bookmarks_dropped > 0U || t0_cleared;
    }

    /// @brief Cold-path status text for the viewer.
    /// @return Empty when nothing was clipped, otherwise a human-readable summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Fixed bitset keyed by a history slot.
class SlotBitset {
public:
    /// @brief Allocate storage for a slot count.
    /// @param slots Number of addressable slots.
    void init(std::size_t slots);
    /// @brief Whether one slot is set.
    /// @param slot Slot index.
    /// @return `true` when set.
    [[nodiscard]] bool test(std::size_t slot) const noexcept;
    /// @brief Set or clear one slot.
    /// @param slot Slot index.
    /// @param value New state.
    /// @return `true` when the stored value changed.
    bool set(std::size_t slot, bool value) noexcept;
    /// @brief Clear every slot.
    void clear() noexcept;
    /// @brief Number of set slots.
    /// @return Popcount.
    [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
    /// @brief Number of addressable slots.
    /// @return Slot count.
    [[nodiscard]] std::size_t slots() const noexcept { return slots_; }

private:
    std::vector<std::uint64_t> words_{}; ///< Bit storage.
    std::size_t slots_{0};               ///< Addressable slots.
    std::uint64_t count_{0};             ///< Set-slot count.
};

/// @brief Independent filters, selection, bookmarks, time, pause, and scope.
///
/// A LIVE view borrows the shared `RecordRing`; a PAUSED view owns an immutable
/// `FrozenHistory` and never changes after creation. Everything else is owned
/// per view, so two views can show different records and selections.
class LogView {
public:
    /// @brief Construct an empty view.
    LogView() = default;
    LogView(const LogView&) = delete;
    LogView& operator=(const LogView&) = delete;

    /// @brief Initialise a LIVE view over the shared ring.
    /// @param id Stable identity.
    /// @param kind Scope kind.
    /// @param source Fixed application code; empty for `All`.
    /// @param title Display title without the hidden window id.
    /// @param ring Shared live history.
    /// @param registry Shared application registry.
    /// @param config Initial filters and modes.
    void init_live(ViewId id, ViewKind kind, std::string source, std::string title,
                   const RecordRing& ring, const AppRegistry& registry, const TimeContext& base,
                   const ViewDefaults& defaults);

    /// @brief Initialise a PAUSED view over an immutable snapshot.
    /// @param id Stable identity.
    /// @param kind Scope kind.
    /// @param source Fixed application code; empty for `All`.
    /// @param title Display title without the hidden window id.
    /// @param frozen Immutable snapshot shared with other paused clones.
    /// @param registry Shared application registry.
    /// @param config Initial filters and modes.
    void init_paused(ViewId id, ViewKind kind, std::string source, std::string title,
                     std::shared_ptr<const FrozenHistory> frozen, const AppRegistry& registry,
                     const TimeContext& base, const ViewDefaults& defaults);

    /// @brief Stable identity.
    /// @return View id.
    [[nodiscard]] ViewId id() const noexcept { return id_; }
    /// @brief Scope kind.
    /// @return View kind.
    [[nodiscard]] ViewKind kind() const noexcept { return kind_; }
    /// @brief Whether the view follows the live ring.
    /// @return `true` for LIVE views.
    [[nodiscard]] bool live() const noexcept { return live_; }
    /// @brief Fixed source, empty for `All`.
    /// @return Borrowed source code.
    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    /// @brief Display title without the hidden id.
    /// @return Borrowed title.
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    /// @brief Immutable snapshot, present only for PAUSED views.
    /// @return Shared snapshot or `nullptr`.
    [[nodiscard]] const std::shared_ptr<const FrozenHistory>& frozen() const noexcept {
        return frozen_;
    }

    /// @brief History backing this view.
    /// @return Borrowed history source.
    [[nodiscard]] const HistorySource& history() const noexcept { return history_; }
    /// @brief Filter index.
    /// @return Mutable reference.
    [[nodiscard]] FilterIndex& filter() noexcept { return filter_; }
    /// @brief Filter index.
    /// @return Const reference.
    [[nodiscard]] const FilterIndex& filter() const noexcept { return filter_; }
    /// @brief Find state.
    /// @return Mutable reference.
    [[nodiscard]] FindState& find() noexcept { return find_; }
    /// @brief Find state.
    /// @return Const reference.
    [[nodiscard]] const FindState& find() const noexcept { return find_; }
    /// @brief Time state with this view's T0 and anomaly threshold.
    /// @return Mutable reference.
    [[nodiscard]] TimeContext& time() noexcept { return time_; }
    /// @brief Time state.
    /// @return Const reference.
    [[nodiscard]] const TimeContext& time() const noexcept { return time_; }
    /// @brief Timestamp display mode.
    /// @return Mutable reference.
    [[nodiscard]] TimestampMode& timestamp_mode() noexcept { return timestamp_mode_; }
    /// @brief Timestamp display mode.
    /// @return Current mode.
    [[nodiscard]] TimestampMode timestamp_mode() const noexcept { return timestamp_mode_; }
    /// @brief Delta display mode.
    /// @return Mutable reference.
    [[nodiscard]] DeltaMode& delta_mode() noexcept { return delta_mode_; }
    /// @brief Delta display mode.
    /// @return Current mode.
    [[nodiscard]] DeltaMode delta_mode() const noexcept { return delta_mode_; }
    /// @brief Minimap counters.
    /// @return Borrowed minimap.
    [[nodiscard]] const Minimap& minimap() const noexcept { return minimap_; }

    /// @brief Start a chunked rebuild over this view's history.
    void begin_rebuild() noexcept;
    /// @brief Scan one bounded rebuild chunk.
    /// @param max_records Records examined in this call.
    void step_rebuild(std::size_t max_records) noexcept;
    /// @brief Whether a rebuild is running.
    /// @return `true` until the scan completes.
    [[nodiscard]] bool rebuilding() const noexcept { return filter_.rebuilding(); }
    /// @brief Run a rebuild to completion (cold-path view creation and cloning).
    void rebuild_now() noexcept;

    /// @brief Account for one newly ingested LIVE record in the minimap.
    /// @param seq Absolute record number.
    void note_ingest(std::uint64_t seq) noexcept;
    /// @brief Append a newly ingested LIVE record when it matches.
    /// @param seq Absolute record number.
    /// @return `true` when the record entered the visible index.
    bool append_live(std::uint64_t seq) noexcept;
    /// @brief Clear per-slot state for an evicted LIVE record.
    /// @param seq Absolute record number.
    /// @param view Record contents before eviction.
    void on_evict(std::uint64_t seq, const RecordView& view) noexcept;
    /// @brief Drop visible numbers that are no longer resident.
    /// @return Number of stale entries removed.
    std::size_t compact() noexcept;

    /// @brief Pause or resume the displayed range.
    /// @param paused New pause state.
    void set_paused(bool paused) noexcept;
    /// @brief Whether the displayed range is paused.
    /// @return Pause state.
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    /// @brief Records retained since the pause started.
    /// @return Count.
    [[nodiscard]] std::uint64_t new_since_pause() const noexcept;
    /// @brief Displayed records overwritten while paused.
    /// @return Count.
    [[nodiscard]] std::uint64_t overwritten_while_paused() const noexcept {
        return pause_overwritten_;
    }
    /// @brief Oldest displayed absolute number.
    /// @return View floor.
    [[nodiscard]] std::uint64_t view_floor() const noexcept;
    /// @brief One past the newest displayed absolute number.
    /// @return View end.
    [[nodiscard]] std::uint64_t view_end() const noexcept;
    /// @brief Number of visible entries in the displayed range.
    /// @return Displayed visible count.
    [[nodiscard]] std::size_t display_count() const noexcept;
    /// @brief Absolute number at one displayed visible order index.
    /// @param index Display order index.
    /// @return Record number, or `0` when out of range.
    [[nodiscard]] std::uint64_t display_at(std::size_t index) const noexcept;

    /// @brief Select only one record.
    /// @param seq Absolute record number.
    void select_only(std::uint64_t seq) noexcept;
    /// @brief Toggle one record's selection.
    /// @param seq Absolute record number.
    void toggle_selection(std::uint64_t seq) noexcept;
    /// @brief Select every record between the anchor and `seq` in visible order.
    /// @param seq Absolute record number.
    void select_to(std::uint64_t seq) noexcept;
    /// @brief Select every visible record.
    void select_all_visible() noexcept;
    /// @brief Clear selection and anchor.
    void clear_selection() noexcept;
    /// @brief Whether a record is selected.
    /// @param seq Absolute record number.
    /// @return `true` when selected.
    [[nodiscard]] bool is_selected(std::uint64_t seq) const noexcept;
    /// @brief Number of selected resident records.
    /// @return Selected count.
    [[nodiscard]] std::uint64_t selected_count() const noexcept { return selection_.count(); }
    /// @brief Last clicked record.
    /// @return Absolute number, or `std::nullopt`.
    [[nodiscard]] std::optional<std::uint64_t> last_selected() const noexcept {
        return last_selected_seq_;
    }
    /// @brief Anchor record selected by the last plain click.
    /// @return Absolute number, or `std::nullopt`.
    [[nodiscard]] std::optional<std::uint64_t> selection_anchor() const noexcept {
        return selection_anchor_;
    }

    /// @brief Toggle a bookmark on one record.
    /// @param seq Absolute record number.
    /// @return New bookmark state.
    bool toggle_bookmark(std::uint64_t seq) noexcept;
    /// @brief Read the bookmark of one record.
    /// @param seq Absolute record number.
    /// @return `true` when bookmarked.
    [[nodiscard]] bool is_bookmarked(std::uint64_t seq) const noexcept;
    /// @brief Number of bookmarked resident records.
    /// @return Bookmark count.
    [[nodiscard]] std::uint64_t bookmarked_count() const noexcept { return bookmarks_.count(); }
    /// @brief Next bookmarked record after `seq`.
    /// @param seq Current record, or `std::nullopt`.
    /// @return Absolute number, or `std::nullopt`.
    std::optional<std::uint64_t> next_bookmark(std::optional<std::uint64_t> seq) const noexcept;
    /// @brief Previous bookmarked record before `seq`.
    /// @param seq Current record, or `std::nullopt`.
    /// @return Absolute number, or `std::nullopt`.
    std::optional<std::uint64_t> previous_bookmark(std::optional<std::uint64_t> seq) const noexcept;
    /// @brief Next Warning/Error/Fatal record.
    /// @param seq Current record, or `std::nullopt`.
    /// @return Absolute number, or `std::nullopt`.
    std::optional<std::uint64_t> next_attention(std::optional<std::uint64_t> seq) const noexcept;
    /// @brief Previous Warning/Error/Fatal record.
    /// @param seq Current record, or `std::nullopt`.
    /// @return Absolute number, or `std::nullopt`.
    std::optional<std::uint64_t>
    previous_attention(std::optional<std::uint64_t> seq) const noexcept;

    /// @brief Copy selected state groups from another view.
    /// @param other Source view.
    /// @param options Checkbox selection.
    /// @return Clipping adjustments applied to the clone.
    [[nodiscard]] CloneAdjustments copy_state_from(const LogView& other,
                                                   const CloneOptions& options) noexcept;
    /// @brief Sync shared time-base fields while keeping this view's T0.
    /// @param base Shared time base.
    void sync_time_base(const TimeContext& base) noexcept;
    /// @brief Reset derived state after the shared live history is cleared.
    void reset_after_clear() noexcept;
    /// @brief Replace filters and modes with defaults, preserving identity and scope.
    /// @param defaults New defaults.
    void reset_to_defaults(const ViewDefaults& defaults) noexcept;
    /// @brief Whether the history contains a frame with one timestamp.
    /// @param stamp_ns Monotonic timestamp.
    /// @return `true` when present.
    [[nodiscard]] bool contains_stamp(std::uint64_t stamp_ns) const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with identity, counts, and filter state.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Apply defaults to filters and modes.
    void apply_defaults(const ViewDefaults& defaults) noexcept;
    /// @brief Reset index, selection, and bookmarks for the current history.
    void allocate_state() noexcept;
    /// @brief Slot for a record, or `slots()` when absent.
    [[nodiscard]] std::size_t slot_of(std::uint64_t seq) const noexcept;
    /// @brief Clear selection/bookmark state for one slot.
    void clear_slot(std::size_t slot) noexcept;
    /// @brief Recompute the paused visible count before `pause_tail_`.
    void refresh_pause_visible_count() noexcept;
    /// @brief Start of the displayed order window.
    [[nodiscard]] std::size_t display_begin() const noexcept;
    /// @brief End of the displayed order window.
    [[nodiscard]] std::size_t display_end() const noexcept;

    /// @brief Copy the clone-selected filter criteria from another view.
    ///
    /// @param other Source view.
    /// @param options Clone selection flags.
    void copy_filter_criteria(const LogView& other, const CloneOptions& options) noexcept;
    /// @brief Copy the clone-selected time settings, validating the T0 anchor.
    ///
    /// @param other Source view.
    /// @param options Clone selection flags.
    /// @return `true` when the T0 anchor was dropped because the new history
    ///         does not contain it.
    [[nodiscard]] bool copy_time_settings(const LogView& other,
                                          const CloneOptions& options) noexcept;
    /// @brief Replay the source view's selection and bookmark flags.
    ///
    /// @param other Source view.
    /// @param options Clone selection flags.
    /// @param selection_copied Receives the number of copied selections.
    /// @param bookmarks_copied Receives the number of copied bookmarks.
    void replay_view_state(const LogView& other, const CloneOptions& options,
                           std::uint64_t& selection_copied,
                           std::uint64_t& bookmarks_copied) noexcept;
    /// @brief Re-run the source view's find query against the new history.
    ///
    /// @param other Source view.
    /// @param options Clone selection flags.
    void rebuild_find(const LogView& other, const CloneOptions& options) noexcept;

    ViewId id_{0};                                          ///< Stable identity.
    ViewKind kind_{ViewKind::All};                          ///< Scope kind.
    bool live_{true};                                       ///< Whether the view follows the ring.
    std::string source_{};                                  ///< Fixed application code.
    std::string title_{};                                   ///< Display title.
    std::shared_ptr<const FrozenHistory> frozen_{};         ///< Paused snapshot.
    const AppRegistry* apps_{nullptr};                      ///< Shared application registry.
    HistorySource history_{};                               ///< Live ring or frozen snapshot.
    FilterIndex filter_{};                                  ///< Visible index.
    FindState find_{};                                      ///< Find state.
    TimeContext time_{};                                    ///< T0, threshold, shared base fields.
    TimestampMode timestamp_mode_{TimestampMode::Absolute}; ///< Timestamp mode.
    DeltaMode delta_mode_{DeltaMode::PreviousVisible};      ///< Delta mode.
    SlotBitset selection_{};                                ///< Per-view selection.
    SlotBitset bookmarks_{};                                ///< Per-view bookmarks.
    Minimap minimap_{};                                     ///< Per-view severity/bookmark map.
    bool paused_{false};                                    ///< Displayed range paused.
    std::uint64_t pause_tail_{0};                           ///< Tail when paused.
    std::uint64_t pause_floor_{0};                          ///< Oldest displayed record.
    std::uint64_t pause_overwritten_{0};                    ///< Displayed records evicted.
    std::size_t pause_visible_count_{0};                    ///< Visible count when paused.
    std::optional<std::uint64_t> selection_anchor_{};       ///< Shift-click anchor.
    std::optional<std::uint64_t> last_selected_seq_{};      ///< Last clicked record.
};

/// @brief Free diagnostic mirror of `LogView::to_string`.
/// @param view View to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const LogView& view);

} // namespace nexigen::logview
