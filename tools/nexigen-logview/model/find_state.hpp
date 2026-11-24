// nexigen-logview — chunked, non-filtering find over one view.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "filter.hpp"
#include "history_source.hpp"

namespace nexigen::logview {

/// @brief Chunked search over one view's visible order.
///
/// Matches are allocated lazily when a query starts, so an idle view does not
/// reserve a full-history match array.
class FindState {
public:
    /// @brief Start a new search over the current visible order.
    /// @param query Case-sensitive substring; empty clears the search.
    /// @param filter Visible index to scan.
    void begin(std::string_view query, const FilterIndex& filter);

    /// @brief Scan one bounded chunk.
    /// @param filter Visible index to scan.
    /// @param source History containing the records.
    /// @param max_records Maximum visible entries examined in this call.
    /// @return `true` when the scan has completed.
    bool step(const FilterIndex& filter, const HistorySource& source, std::size_t max_records);

    /// @brief Whether a scan is still running.
    /// @return `true` until the fixed target is reached.
    [[nodiscard]] bool active() const noexcept { return active_; }
    /// @brief Search text.
    /// @return Borrowed query.
    [[nodiscard]] const std::string& query() const noexcept { return query_; }
    /// @brief Number of matches found.
    /// @return Match count.
    [[nodiscard]] std::size_t count() const noexcept { return matches_.size(); }
    /// @brief Matches found so far.
    /// @return Borrowed array.
    [[nodiscard]] const std::vector<std::uint64_t>& matches() const noexcept { return matches_; }

    /// @brief Select the next match, wrapping at the end.
    /// @return The selected absolute number, or `std::nullopt` when empty.
    std::optional<std::uint64_t> next() noexcept;
    /// @brief Select the previous match, wrapping at the front.
    /// @return The selected absolute number, or `std::nullopt` when empty.
    std::optional<std::uint64_t> previous() noexcept;

    /// @brief Currently selected match.
    /// @return Absolute number, or `std::nullopt` before the first navigation.
    [[nodiscard]] std::optional<std::uint64_t> current() const noexcept;
    /// @brief Whether a record number is a match.
    /// @param seq Absolute record number.
    /// @return `true` when found in the match array.
    [[nodiscard]] bool contains(std::uint64_t seq) const noexcept;
    /// @brief Reset the search and clear matches.
    void clear() noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with query and counts.
    [[nodiscard]] std::string to_string() const;

private:
    std::string query_{};                  ///< Search text.
    std::vector<std::uint64_t> matches_{}; ///< Lazy match array.
    std::size_t scan_{0};                  ///< Next visible order index.
    std::size_t target_{0};                ///< Fixed visible count for this scan.
    std::size_t current_{0};               ///< Current match index.
    bool active_{false};                   ///< Scan running.
    bool have_current_{false};             ///< Whether `current_` is meaningful.
};

/// @brief Free diagnostic mirror of `FindState::to_string`.
/// @param find Find state to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const FindState& find);

} // namespace nexigen::logview
