// nexigen-canview — per-view circular visible index.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "filter_criteria.hpp"
#include "record_source.hpp"
#include "view_kind.hpp"

namespace nexigen::canview {

/// @brief Immutable scope of one view.
struct ViewScope {
    ViewKind kind{ViewKind::All};             ///< Scope kind.
    std::uint16_t channel_id{0};              ///< Fixed channel for Channel/Node.
    std::optional<std::uint16_t> node_slot{}; ///< Resolved node slot for Node.

    /// @brief Whether a record belongs to this scope.
    /// @param record Record to test.
    /// @return `true` when in scope; loss/bus-idle records match by channel.
    [[nodiscard]] bool contains(const CanRecordView& record) const noexcept;
};

/// @brief Ordered records that pass one view's filter.
///
/// Entries store the low 32 bits of the absolute sequence (4 B per slot); the
/// full number is reconstructed against the source's 64-bit head. New matching
/// records append in O(1); a criteria change scans at most one bounded chunk
/// per rendered frame.
class VisibleIndex {
public:
    /// @brief Progress of one rebuild.
    struct Progress {
        std::uint64_t scanned{0}; ///< Records examined.
        std::uint64_t total{0};   ///< Records targeted.
        bool active{false};       ///< Rebuild running.

        /// @brief Completion ratio in `0.0..1.0`.
        /// @return Scanned divided by total, or `1.0` when inactive.
        [[nodiscard]] double ratio() const noexcept;
        /// @brief Cold-path diagnostic rendering.
        /// @return Text with scanned, total, and active state.
        [[nodiscard]] std::string to_string() const;
    };

    /// @brief Allocate the fixed visible storage.
    /// @param capacity Maximum visible entries.
    void init(std::size_t capacity);

    /// @brief Set the immutable scope.
    /// @param scope New scope; also clears the visible entries.
    void set_scope(const ViewScope& scope) noexcept;
    /// @brief Current scope.
    /// @return Scope value.
    [[nodiscard]] const ViewScope& scope() const noexcept { return scope_; }
    /// @brief Whether a record is inside the scope.
    /// @param record Record to test.
    /// @return `true` when in scope.
    [[nodiscard]] bool in_scope(const CanRecordView& record) const noexcept {
        return scope_.contains(record);
    }

    /// @brief Mutable criteria.
    /// @return Criteria reference.
    [[nodiscard]] FilterCriteria& criteria() noexcept { return criteria_; }
    /// @brief Const criteria.
    /// @return Criteria reference.
    [[nodiscard]] const FilterCriteria& criteria() const noexcept { return criteria_; }

    /// @brief Start a rebuild over one source.
    /// @param source History to scan.
    void begin_rebuild(const RecordSource& source) noexcept;

    /// @brief Scan one bounded chunk of the rebuild.
    /// @param source History to scan.
    /// @param max_records Maximum records examined in this call.
    /// @param labels Aliases used by the textual filter.
    /// @return Progress after the chunk.
    Progress step_rebuild(const RecordSource& source, std::size_t max_records,
                          const FilterLabels& labels = {});

    /// @brief Whether a rebuild is in progress.
    /// @return `true` until the scan reaches its target.
    [[nodiscard]] bool rebuilding() const noexcept { return active_; }
    /// @brief Current progress snapshot.
    /// @return Progress value.
    [[nodiscard]] Progress progress() const noexcept;

    /// @brief Append a new record when it matches.
    /// @param source History containing the record.
    /// @param seq Absolute record number.
    /// @param labels Aliases used by the textual filter.
    /// @return `true` when visible and appended.
    bool append_if_visible(const RecordSource& source, std::uint64_t seq,
                           const FilterLabels& labels = {});

    /// @brief Clear the visible entries without changing criteria.
    void clear() noexcept;

    /// @brief Low 32 bits of one visible entry.
    /// @param index Order index.
    /// @return Stored value, or zero when out of range.
    [[nodiscard]] std::uint32_t visible_low_at(std::size_t index) const noexcept;
    /// @brief Resolve one visible entry to an absolute sequence.
    /// @param index Order index.
    /// @param source History defining the 64-bit base.
    /// @return Absolute record number, or zero when out of range.
    [[nodiscard]] std::uint64_t visible_seq_at(std::size_t index,
                                               const RecordSource& source) const noexcept;
    /// @brief Number of entries.
    /// @return Visible count.
    [[nodiscard]] std::size_t visible_count() const noexcept { return visible_count_; }
    /// @brief Allocated capacity.
    /// @return Maximum entries.
    [[nodiscard]] std::size_t capacity() const noexcept { return visible_.size(); }

    /// @brief Drop entries that are no longer resident.
    /// @param source History to test.
    /// @return Number of entries removed.
    std::size_t compact(const RecordSource& source) noexcept;
    /// @brief Count entries that are no longer resident.
    /// @param source History to test.
    /// @return Stale count.
    [[nodiscard]] std::size_t stale(const RecordSource& source) const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with criteria and progress.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Append one low sequence value.
    void append(std::uint64_t seq) noexcept;
    /// @brief Scope plus criteria for one record.
    [[nodiscard]] bool matches_record(const RecordSource& source, std::uint64_t seq,
                                      const FilterLabels& labels) const noexcept;

    FilterCriteria criteria_{};            ///< Active criteria.
    std::vector<std::uint32_t> visible_{}; ///< Circular low-sequence storage.
    std::size_t visible_start_{0};         ///< First live entry.
    std::size_t visible_count_{0};         ///< Live entries.
    bool active_{false};                   ///< Rebuild running.
    std::uint64_t scan_seq_{0};            ///< Next absolute sequence.
    std::uint64_t target_seq_{0};          ///< Tail captured at rebuild start.
    std::size_t target_{0};                ///< Source size at rebuild start.
    ViewScope scope_{};                    ///< Immutable view scope.
};

} // namespace nexigen::canview
