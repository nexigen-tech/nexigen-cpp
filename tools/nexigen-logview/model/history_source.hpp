// nexigen-logview — shared history sources for live and frozen log views.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/log_frame.hpp>

#include "record_ring.hpp"
#include "record_view.hpp"

namespace nexigen::logview {

/// @brief Maximum bytes of copied record data in one frozen view.
inline constexpr std::size_t kMaxSnapshotRecordBytes = 1024U * 1024U;

/// @brief Per-record storage charged against the snapshot budget.
inline constexpr std::size_t kSnapshotRecordBytes =
    sizeof(nexigen::logging::LogFrame) + sizeof(std::uint8_t) + sizeof(std::int64_t);

/// @brief Immutable history copied from the live ring for a PAUSED view.
class FrozenHistory {
public:
    /// @brief Capture the newest records of one source (or every source).
    /// @param ring Live history to read.
    /// @param source Application id; ignored when `all` is true.
    /// @param all Copy every source and every gap in the selected range.
    /// @return Frozen history, or an invalid-argument failure.
    [[nodiscard]] static std::shared_ptr<FrozenHistory> capture(const RecordRing& ring,
                                                                std::string_view source, bool all);

    /// @brief Number of retained records.
    /// @return Record count.
    [[nodiscard]] std::size_t size() const noexcept { return seqs_.size(); }
    /// @brief First absolute sequence in the snapshot.
    /// @return First sequence, or `0` when empty.
    [[nodiscard]] std::uint64_t head() const noexcept { return seqs_.empty() ? 0U : seqs_.front(); }
    /// @brief One past the last sequence in the snapshot.
    /// @return Last sequence plus one, or `0` when empty.
    [[nodiscard]] std::uint64_t tail() const noexcept {
        return seqs_.empty() ? 0U : seqs_.back() + 1U;
    }
    /// @brief Absolute sequence at one slot.
    /// @param index Slot index.
    /// @return Sequence number.
    [[nodiscard]] std::uint64_t seq_at(std::size_t index) const noexcept { return seqs_[index]; }
    /// @brief Read one retained record by absolute sequence.
    /// @param seq Absolute sequence number.
    /// @return Borrowed view, or `std::nullopt` when absent.
    [[nodiscard]] std::optional<RecordView> find(std::uint64_t seq) const noexcept;
    /// @brief Find the slot for an absolute sequence.
    /// @param seq Absolute sequence number.
    /// @return Slot index, or `std::nullopt` when absent.
    [[nodiscard]] std::optional<std::size_t> slot_for(std::uint64_t seq) const noexcept;

private:
    FrozenHistory() = default;

    std::vector<nexigen::logging::LogFrame> frames_{};
    std::vector<std::uint8_t> kinds_{};
    std::vector<std::int64_t> prev_{};
    std::vector<std::uint64_t> seqs_{};
};

/// @brief Borrowed view over either the live ring or a frozen snapshot.
class HistorySource {
public:
    /// @brief Construct an empty live source.
    HistorySource() = default;

    /// @brief Point at the live ring.
    /// @param ring Live history.
    /// @return Source view.
    [[nodiscard]] static HistorySource live(const RecordRing& ring) noexcept;

    /// @brief Point at a frozen snapshot.
    /// @param history Frozen history.
    /// @return Source view.
    [[nodiscard]] static HistorySource frozen(const FrozenHistory& history) noexcept;

    /// @brief Whether this source is the live ring.
    [[nodiscard]] bool is_live() const noexcept { return live_ != nullptr; }
    /// @brief First retained absolute sequence.
    [[nodiscard]] std::uint64_t head() const noexcept;
    /// @brief One past the newest absolute sequence.
    [[nodiscard]] std::uint64_t tail() const noexcept;
    /// @brief Number of retained records.
    [[nodiscard]] std::size_t size() const noexcept;
    /// @brief Number of selection/bookmark slots.
    [[nodiscard]] std::size_t slot_count() const noexcept;
    /// @brief Absolute sequence at an ordered source index.
    [[nodiscard]] std::uint64_t seq_at(std::size_t index) const noexcept;
    /// @brief Find one record.
    [[nodiscard]] std::optional<RecordView> find(std::uint64_t seq) const noexcept;
    /// @brief Find the slot for one record.
    [[nodiscard]] std::optional<std::size_t> slot_for(std::uint64_t seq) const noexcept;
    /// @brief Whether a record is retained.
    [[nodiscard]] bool resident(std::uint64_t seq) const noexcept;

private:
    const RecordRing* live_{nullptr};
    const FrozenHistory* frozen_{nullptr};
};

} // namespace nexigen::logview
