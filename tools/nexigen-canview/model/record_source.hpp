// nexigen-canview — read-only source of retained records.
#pragma once

#include <cstdint>
#include <optional>

#include "can_record_ring.hpp"
#include "can_record_view.hpp"

namespace nexigen::canview {

/// @brief Abstract history source consumed by filters, indexes and views.
///
/// A LIVE view borrows the shared ring through `RingRecordSource`; a PAUSED
/// view owns an immutable `SnapshotSource`. Both expose the same absolute
/// sequence space.
class RecordSource {
public:
    virtual ~RecordSource() = default;

    /// @brief Read one record by absolute number.
    /// @param seq Absolute record number.
    /// @return The view when resident.
    [[nodiscard]] virtual std::optional<CanRecordView> find(std::uint64_t seq) const noexcept = 0;
    /// @brief Oldest retained absolute number.
    /// @return Head sequence.
    [[nodiscard]] virtual std::uint64_t head() const noexcept = 0;
    /// @brief One past the newest absolute number.
    /// @return Tail sequence.
    [[nodiscard]] virtual std::uint64_t tail() const noexcept = 0;

    /// @brief Number of retained records.
    /// @return `tail - head`.
    [[nodiscard]] std::uint64_t size() const noexcept { return tail() - head(); }
    /// @brief Whether a record number is resident.
    /// @param seq Absolute record number.
    /// @return `true` when within the retained range.
    [[nodiscard]] bool resident(std::uint64_t seq) const noexcept {
        return seq >= head() && seq < tail();
    }
};

/// @brief `RecordSource` over the shared live ring.
class RingRecordSource final : public RecordSource {
public:
    /// @brief Bind to the shared ring.
    /// @param ring Ring that must outlive this source.
    explicit RingRecordSource(const CanRecordRing& ring) noexcept : ring_(&ring) {}

    [[nodiscard]] std::optional<CanRecordView> find(std::uint64_t seq) const noexcept override {
        return ring_->find(seq);
    }
    [[nodiscard]] std::uint64_t head() const noexcept override { return ring_->head(); }
    [[nodiscard]] std::uint64_t tail() const noexcept override { return ring_->tail(); }

private:
    const CanRecordRing* ring_{nullptr}; ///< Borrowed history.
};

} // namespace nexigen::canview
