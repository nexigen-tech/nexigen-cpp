// nexigen-canview — items handed from capture to the UI thread.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_message.hpp>

#include "gap_reason.hpp"
#include "record_kind.hpp"

namespace nexigen::canview {

/// @brief One frame or loss marker waiting for the UI thread.
///
/// The capture thread is the sole producer and the UI thread the sole consumer,
/// so the fixed queue holds this plain value without synchronization beyond the
/// queue's own acquire/release pair. The struct is cache-line aligned and
/// occupies exactly two cache lines.
struct alignas(64) CanIngressRecord {
    nexigen::hw::can::CanMessage msg{};       ///< Received message or synthetic marker.
    std::uint64_t lost_before{0};             ///< Missing records reported before this one.
    std::uint16_t time_epoch{0};              ///< UTC anchor epoch at ingest.
    CanRecordKind kind{CanRecordKind::Frame}; ///< Frame, bus-idle or loss kind.
    std::uint8_t co_map_epoch{0};             ///< CANopen mapping snapshot.
    std::uint8_t flags{0};                    ///< Packed `record_meta` flag bits.
    std::uint8_t anchor_source{0};            ///< Time-anchor provenance.
    std::array<std::uint8_t, 2> reserved{};   ///< Reserved for future flags.

    /// @brief Bits 0..2 of `flags` hold a synthetic record's `GapReason`.
    static constexpr std::uint8_t gap_reason_mask{0x07U};
    /// @brief Bit 3 of `flags` marks a synthetic gap count as exact.
    static constexpr std::uint8_t count_exact_bit{0x08U};

    /// @brief Pack the synthetic-loss flags.
    /// @param reason Why the loss happened.
    /// @param count_exact Whether the missing count is exact.
    /// @return Flags byte for a synthetic record.
    [[nodiscard]] static constexpr std::uint8_t pack_gap_flags(GapReason reason,
                                                               bool count_exact) noexcept {
        return static_cast<std::uint8_t>((static_cast<std::uint8_t>(reason) & gap_reason_mask) |
                                         (count_exact ? count_exact_bit : 0U));
    }

    /// @brief Gap reason of a synthetic record.
    /// @return Reason stored in the flags byte.
    [[nodiscard]] constexpr GapReason gap_reason() const noexcept {
        return static_cast<GapReason>(flags & gap_reason_mask);
    }

    /// @brief Whether a synthetic gap count is exact.
    /// @return `true` when the count-exact bit is set.
    [[nodiscard]] constexpr bool gap_count_exact() const noexcept {
        return (flags & count_exact_bit) != 0U;
    }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text naming the kind and the CAN identity.
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(nexigen::hw::can::CanMessage) == 112U, "CanMessage must stay 112 bytes");
static_assert(sizeof(CanIngressRecord) == 128U, "CanIngressRecord must be exactly 128 bytes");
static_assert(alignof(CanIngressRecord) == 64U, "CanIngressRecord must be cache-line aligned");
static_assert(offsetof(CanIngressRecord, msg) == 0U);
static_assert(offsetof(CanIngressRecord, lost_before) == 112U);
// `time_epoch` sits first after `lost_before` so the `uint16_t` stays naturally
// aligned; the remaining seven bytes keep the slot at exactly 128 bytes.
static_assert(offsetof(CanIngressRecord, time_epoch) == 120U);
static_assert(offsetof(CanIngressRecord, kind) == 122U);
static_assert(offsetof(CanIngressRecord, co_map_epoch) == 123U);
static_assert(offsetof(CanIngressRecord, flags) == 124U);
static_assert(offsetof(CanIngressRecord, anchor_source) == 125U);
static_assert(offsetof(CanIngressRecord, reserved) == 126U);

/// @brief One channel-status sample waiting for the UI thread.
struct alignas(8) StatusEvent {
    std::uint16_t channel_id{0};                 ///< Channel the sample belongs to.
    std::uint8_t flags{0};                       ///< Reserved status flags.
    nexigen::hw::can::CanChannelStatus status{}; ///< Driver snapshot.
};

static_assert(sizeof(StatusEvent) == 80U, "StatusEvent must stay 80 bytes");
static_assert(alignof(StatusEvent) == 8U, "StatusEvent must stay 8-byte aligned");

/// @brief One UTC anchor estimate waiting for the UI thread.
struct alignas(8) AnchorEvent {
    std::uint16_t channel_id{0};       ///< Channel the anchor belongs to.
    std::uint16_t epoch{0};            ///< Anchor epoch index.
    std::uint8_t source{0};            ///< Anchor source (time base, fallback).
    std::uint8_t flags{0};             ///< Reserved anchor flags.
    std::int64_t utc_offset_ns{0};     ///< UTC minus hardware time.
    std::int32_t drift_ppb{0};         ///< Estimated clock drift.
    std::uint64_t valid_from_hw_ns{0}; ///< First hardware time using this anchor.
};

static_assert(sizeof(AnchorEvent) == 32U, "AnchorEvent must stay 32 bytes");
static_assert(alignof(AnchorEvent) == 8U, "AnchorEvent must stay 8-byte aligned");

} // namespace nexigen::canview
