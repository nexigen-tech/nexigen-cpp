// nexigen-logview — filter criteria and per-view visible index.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/log_frame.hpp>

#include "app_registry.hpp"
#include "history_source.hpp"

namespace nexigen::logview {

/// @brief Active source/level/text filter.
struct FilterCriteria {
    /// @brief Per-level visibility, indexed by `core::LogLevel`.
    std::array<bool, 7> levels{true, true, true, true, true, true, true};
    /// @brief Per-application visibility keyed by the stable registry slot.
    std::array<bool, AppRegistry::capacity> apps{};
    /// @brief Case-sensitive message substring; empty disables the text filter.
    std::string text{};
    /// @brief Fixed application scope; empty means every source.
    std::string source{};
    /// @brief Whether at least one application is hidden.
    bool app_filter_active{false};

    /// @brief Construct criteria with every source visible.
    FilterCriteria() noexcept { apps.fill(true); }

    /// @brief Whether a received frame passes the filter.
    /// @param frame Frame to test.
    /// @param registry Application table used to resolve the source slot.
    /// @return `true` when the frame is visible.
    [[nodiscard]] bool matches(const nexigen::logging::LogFrame& frame,
                               const AppRegistry& registry) const noexcept;

    /// @brief Whether one history record passes these criteria.
    /// @param source History containing the record.
    /// @param registry Application table used to resolve the source slot.
    /// @param seq Absolute record number.
    /// @return `true` when the record is visible in this view.
    [[nodiscard]] bool matches(const HistorySource& source, const AppRegistry& registry,
                               std::uint64_t seq) const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the text filter and hidden-source state.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Ordered absolute record numbers that pass one view's filter.
///
/// The index owns its visible storage. New matching records append in O(1); a
/// criterion change scans a bounded chunk per frame. Evicted numbers are
/// skipped at render time and compacted when they become numerous.
class FilterIndex {
public:
    /// @brief Progress of one rebuild.
    struct Progress {
        std::uint64_t scanned{0}; ///< Records examined by the rebuild.
        std::uint64_t total{0};   ///< Records targeted by the rebuild.
        bool active{false};       ///< Whether a rebuild is running.

        /// @brief Completion ratio in `0.0..1.0`.
        /// @return Scanned divided by total, or `1.0` when inactive.
        [[nodiscard]] double ratio() const noexcept;
        /// @brief Cold-path diagnostic rendering.
        /// @return Text with scanned, total, and active state.
        [[nodiscard]] std::string to_string() const;
    };

    /// @brief Allocate the fixed visible storage for one view.
    /// @param capacity Maximum visible entries.
    void init(std::size_t capacity, const AppRegistry& apps);

    /// @brief Mutable criteria.
    /// @return Criteria reference.
    [[nodiscard]] FilterCriteria& criteria() noexcept { return criteria_; }
    /// @brief Const criteria.
    /// @return Criteria reference.
    [[nodiscard]] const FilterCriteria& criteria() const noexcept { return criteria_; }

    /// @brief Start a rebuild over one history source.
    /// @param source History to scan.
    void begin_rebuild(const HistorySource& source) noexcept;

    /// @brief Scan one bounded chunk of the rebuild.
    /// @param source History to scan.
    /// @param max_records Maximum records examined in this call.
    /// @return Progress after the chunk.
    Progress step_rebuild(const HistorySource& source, std::size_t max_records) noexcept;

    /// @brief Whether a rebuild is in progress.
    /// @return `true` until the scan reaches its target.
    [[nodiscard]] bool rebuilding() const noexcept { return active_; }

    /// @brief Current progress snapshot.
    /// @return Progress value.
    [[nodiscard]] Progress progress() const noexcept;

    /// @brief Append a new record when it matches the active filter.
    /// @param source History containing the record.
    /// @param registry Application table used to resolve the source slot.
    /// @param seq Absolute record number.
    /// @return `true` when the record was visible and appended.
    bool append_if_visible(const HistorySource& source, std::uint64_t seq) noexcept;

    /// @brief Clear the visible index without changing criteria.
    void clear() noexcept;

    /// @brief Read one visible entry by order.
    /// @param index Order index.
    /// @return Absolute record number, or `0` when out of range.
    [[nodiscard]] std::uint64_t visible_at(std::size_t index) const noexcept;
    /// @brief Number of entries in the visible index.
    /// @return Visible count.
    [[nodiscard]] std::size_t visible_count() const noexcept { return visible_count_; }
    /// @brief Allocated visible capacity.
    /// @return Maximum entries before the oldest is dropped.
    [[nodiscard]] std::size_t capacity() const noexcept { return visible_.size(); }
    /// @brief Drop visible numbers that are no longer resident.
    /// @param source History to test.
    /// @return Number of stale entries removed.
    std::size_t compact(const HistorySource& source) noexcept;
    /// @brief Count visible numbers that are no longer resident.
    /// @param source History to test.
    /// @return Stale entry count.
    [[nodiscard]] std::size_t stale(const HistorySource& source) const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with criteria and progress.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Append one absolute number when there is room.
    void append(std::uint64_t seq) noexcept;

    FilterCriteria criteria_{};            ///< Active criteria.
    const AppRegistry* apps_{nullptr};     ///< Shared application registry.
    std::vector<std::uint64_t> visible_{}; ///< Fixed visible storage.
    std::size_t visible_start_{0};         ///< First live visible entry.
    std::size_t visible_count_{0};         ///< Number of live visible entries.
    bool active_{false};                   ///< Rebuild running.
    bool live_rebuild_{false};             ///< Rebuild scans absolute LIVE sequences.
    std::size_t scan_{0};                  ///< Next ordinal source index to scan.
    std::size_t target_{0};                ///< Ordinal source count targeted by the rebuild.
    std::uint64_t scan_seq_{0};            ///< Next absolute sequence for a LIVE rebuild.
    std::uint64_t target_seq_{0};          ///< Absolute tail captured at rebuild start.
};

/// @brief One view's filtered visible index.
using ViewIndex = FilterIndex;

/// @brief Free diagnostic mirror of `FilterIndex::to_string`.
/// @param filter Filter to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const FilterIndex& filter);

} // namespace nexigen::logview
