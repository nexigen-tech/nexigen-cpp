// nexigen-canview — immutable PAUSED snapshot source.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <nexigen/hw/can/can_message.hpp>

#include "can_record_view.hpp"
#include "record_source.hpp"

namespace nexigen::canview {

/// @brief One owned record inside a PAUSED snapshot.
struct SnapshotRecord {
    std::uint64_t seq{0};                   ///< Original absolute number.
    nexigen::hw::can::CanMessage message{}; ///< Owned payload.
    std::int64_t delta_same_id_ns{0};       ///< Per-key delta or gap count.
    std::uint16_t co_node_index{0};         ///< CANopen registry slot.
    std::uint8_t meta{0};                   ///< Packed metadata.
    std::uint16_t time_epoch{0};            ///< UTC anchor epoch.
    std::uint8_t co_map_epoch{0};           ///< CANopen mapping epoch.
};

/// @brief `RecordSource` that owns a bounded copy of live history.
///
/// Built newest-to-oldest up to a byte budget and materialised in ring order,
/// so later traffic cannot change a PAUSED clone.
class SnapshotSource final : public RecordSource {
public:
    /// @brief Destroy the snapshot source.
    ~SnapshotSource() override;
    /// @brief Copy at most `max_bytes` of record data from `source`.
    /// @param source Live history to copy.
    /// @param max_bytes Byte budget; each record costs `sizeof(CanIngressRecord)`.
    void build(const RecordSource& source, std::uint64_t max_bytes);

    /// @brief Read one snapshot record.
    /// @param seq Original absolute number.
    /// @return The view when present.
    [[nodiscard]] std::optional<CanRecordView> find(std::uint64_t seq) const noexcept override;
    /// @brief Oldest snapshot sequence, or zero when empty.
    [[nodiscard]] std::uint64_t head() const noexcept override;
    /// @brief One past the newest snapshot sequence.
    [[nodiscard]] std::uint64_t tail() const noexcept override;

    /// @brief Number of copied records.
    /// @return Snapshot size.
    [[nodiscard]] std::size_t record_count() const noexcept { return records_.size(); }
    /// @brief Bytes consumed by owned records.
    /// @return `record_count() * sizeof(SnapshotRecord)`.
    [[nodiscard]] std::size_t bytes() const noexcept {
        return records_.size() * sizeof(SnapshotRecord);
    }

private:
    std::vector<SnapshotRecord> records_{}; ///< Ascending by original sequence.
};

} // namespace nexigen::canview
