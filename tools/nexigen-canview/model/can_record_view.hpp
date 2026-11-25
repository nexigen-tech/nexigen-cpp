// nexigen-canview — borrowed view of one retained record.
#pragma once

#include <cstdint>
#include <string>

#include <nexigen/hw/can/can_message.hpp>

#include "record_kind.hpp"
#include "record_meta.hpp"

namespace nexigen::canview {

/// @brief Borrowed view of one history slot.
///
/// The view points into the ring and is valid until the next ingest. Render and
/// inspector code reads it immediately and never stores it.
struct CanRecordView {
    /// @brief `co_node_index` value for a record without a CANopen binding.
    static constexpr std::uint16_t no_node{0xFFFFU};

    std::uint64_t seq{0};                          ///< Absolute record number.
    const nexigen::hw::can::CanMessage* message{}; ///< Borrowed frame payload.
    std::int64_t delta_same_id_ns{0};              ///< Per-key delta, or gap count.
    std::uint16_t co_node_index{no_node};          ///< CANopen registry slot, or `no_node`.
    std::uint8_t meta{0};                          ///< Packed `record_meta`.
    std::uint16_t time_epoch{0};                   ///< UTC anchor epoch at ingest.
    std::uint8_t co_map_epoch{0};                  ///< CANopen mapping snapshot.

    /// @brief Record kind from the packed metadata.
    /// @return `CanRecordKind`.
    [[nodiscard]] CanRecordKind kind() const noexcept { return meta_kind(meta); }

    /// @brief Whether the view is a received frame.
    /// @return `true` for `CanRecordKind::Frame`.
    [[nodiscard]] bool is_frame() const noexcept { return kind() == CanRecordKind::Frame; }

    /// @brief Missing count for synthetic records.
    /// @return `delta_same_id_ns` as an unsigned count.
    [[nodiscard]] std::uint64_t gap_count() const noexcept {
        return delta_same_id_ns < 0 ? 0U : static_cast<std::uint64_t>(delta_same_id_ns);
    }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text naming the sequence, kind, and CAN identity.
    [[nodiscard]] std::string to_string() const;
};

} // namespace nexigen::canview
