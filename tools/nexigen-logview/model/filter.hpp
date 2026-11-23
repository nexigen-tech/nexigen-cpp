// nexigen-logview — filter criteria and incremental visible index.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/log_frame.hpp>

#include "app_registry.hpp"
#include "record_ring.hpp"

namespace nexigen::logview {

/// @brief Active source/level/text filter.
struct FilterCriteria {
    /// @brief Per-level visibility, indexed by `core::LogLevel`.
    std::array<bool, 7> levels{true, true, true, true, true, true, true};
    /// @brief Per-application visibility keyed by the stable registry slot.
    std::array<bool, AppRegistry::capacity> apps{};
    /// @brief Case-sensitive message substring; empty disables the text filter.
    std::string text{};
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

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the text filter and hidden-source state.
    [[nodiscard]] std::string to_string() const {
        return std::format("FilterCriteria(text={}, app_filter={}, levels={})", text,
                           app_filter_active, levels.size());
    }
};

/// @brief Ordered absolute record numbers that pass the active filter.
///
/// New matching records are appended in O(1); a criterion change starts a
/// chunked rebuild that scans at most `max_records` per frame and reports
/// progress. Evicted numbers are skipped at render time and compacted when they
/// become numerous.
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
        [[nodiscard]] std::string to_string() const {
            return std::format("Progress(scanned={}, total={}, active={})", scanned, total, active);
        }
    };

    /// @brief Mutable criteria.
    /// @return Criteria reference.
    [[nodiscard]] FilterCriteria& criteria() noexcept { return criteria_; }
    /// @brief Const criteria.
    /// @return Criteria reference.
    [[nodiscard]] const FilterCriteria& criteria() const noexcept { return criteria_; }

    /// @brief Start a rebuild over the current resident range.
    /// @param ring History to scan.
    void begin_rebuild(const RecordRing& ring) noexcept;

    /// @brief Scan one bounded chunk of the rebuild.
    /// @param ring History to scan.
    /// @param max_records Maximum records examined in this call.
    /// @return Progress after the chunk.
    Progress step_rebuild(RecordRing& ring, std::size_t max_records) noexcept;

    /// @brief Whether a rebuild is in progress.
    /// @return `true` until the scan reaches its target.
    [[nodiscard]] bool rebuilding() const noexcept { return active_; }

    /// @brief Current progress snapshot.
    /// @return Progress value.
    [[nodiscard]] Progress progress() const noexcept;

    /// @brief Append a new record when it matches the active filter.
    /// @param ring History containing the record.
    /// @param seq Absolute record number.
    void append_if_visible(RecordRing& ring, std::uint64_t seq) noexcept;

    /// @brief Clear the visible index without changing criteria.
    /// @param ring History owning the index.
    void clear(RecordRing& ring) noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with criteria and progress.
    [[nodiscard]] std::string to_string() const;

private:
    FilterCriteria criteria_{};      ///< Active criteria.
    bool active_{false};             ///< Rebuild running.
    std::uint64_t scan_{0};          ///< Next absolute number to scan.
    std::uint64_t target_{0};        ///< One past the last number to scan.
    bool ring_clear_pending_{false}; ///< Visible index still holds the previous result.
};

/// @brief Free diagnostic mirror of `FilterIndex::to_string`.
/// @param filter Filter to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const FilterIndex& filter);

} // namespace nexigen::logview
