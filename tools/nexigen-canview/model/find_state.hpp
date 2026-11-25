// nexigen-canview — incremental find over visible records.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "filter_criteria.hpp"
#include "record_source.hpp"
#include "visible_index.hpp"

namespace nexigen::canview {

/// @brief Find state independent of the view filter.
///
/// Find never hides records: it records matching absolute sequences in visible
/// order and exposes next/previous navigation. The matcher only reuses the
/// textual half of `FilterCriteria`.
class FindState {
public:
    /// @brief Construct a matcher that shows every record class.
    FindState();

    /// @brief Set the query and invalidate previous matches.
    /// @param value New query; an empty string clears the matches.
    void set_query(std::string value);

    /// @brief Current query.
    /// @return Query text.
    [[nodiscard]] const std::string& query() const noexcept { return query_; }

    /// @brief Start a chunked rebuild from the visible order.
    /// @param index Visible index of the view.
    void begin_rebuild(const VisibleIndex& index) noexcept;

    /// @brief Scan one bounded chunk of the current rebuild.
    /// @param source History to read.
    /// @param index Visible index of the view.
    /// @param labels Aliases used by the textual matcher.
    /// @param max_records Maximum records examined in this call.
    /// @return `true` while more records remain.
    bool step_rebuild(const RecordSource& source, const VisibleIndex& index,
                      const FilterLabels& labels, std::size_t max_records);

    /// @brief Rebuild matches from the visible order in one call (tests).
    /// @param source History to read.
    /// @param index Visible index of the view.
    /// @param labels Aliases used by the textual matcher.
    void rebuild(const RecordSource& source, const VisibleIndex& index, const FilterLabels& labels);

    /// @brief Clear matches and the cursor.
    void clear() noexcept;

    /// @brief Number of matches.
    /// @return Match count.
    [[nodiscard]] std::size_t count() const noexcept { return matches_.size(); }
    /// @brief Current cursor position.
    /// @return Cursor, zero when there are no matches.
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
    /// @brief Whether matches are available.
    /// @return `true` when at least one record matched.
    [[nodiscard]] bool active() const noexcept { return !matches_.empty(); }
    /// @brief Whether a chunked rebuild is still running.
    /// @return `true` between `begin_rebuild()` and the last chunk.
    [[nodiscard]] bool rebuilding() const noexcept { return scanning_; }
    /// @brief Records examined by the current rebuild.
    /// @return Scanned count.
    [[nodiscard]] std::size_t scanned() const noexcept { return scan_order_; }
    /// @brief Records targeted by the current rebuild.
    /// @return Total count.
    [[nodiscard]] std::size_t total() const noexcept { return scan_total_; }

    /// @brief Currently focused match.
    /// @return Absolute sequence or `std::nullopt`.
    [[nodiscard]] std::optional<std::uint64_t> current() const noexcept;
    /// @brief Advance cyclically to the next match.
    /// @return Absolute sequence or `std::nullopt`.
    std::optional<std::uint64_t> next() noexcept;
    /// @brief Move cyclically to the previous match.
    /// @return Absolute sequence or `std::nullopt`.
    std::optional<std::uint64_t> previous() noexcept;

    /// @brief Whether one record is a match.
    /// @param seq Absolute record number.
    /// @return `true` when the sequence is in the match list.
    [[nodiscard]] bool contains(std::uint64_t seq) const noexcept;

private:
    FilterCriteria matcher_{};             ///< Text-only matcher.
    std::string query_{};                  ///< Current query.
    std::vector<std::uint64_t> matches_{}; ///< Matching sequences in visible order.
    std::size_t cursor_{0};                ///< Current match position.
    std::size_t scan_order_{0};            ///< Next visible order to examine.
    std::size_t scan_total_{0};            ///< Visible order at rebuild start.
    bool scanning_{false};                 ///< Chunked rebuild in progress.
};

} // namespace nexigen::canview
