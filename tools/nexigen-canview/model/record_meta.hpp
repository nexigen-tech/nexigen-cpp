// nexigen-canview — one-byte record metadata packing (change add-can-monitor-view).
#pragma once

#include <cstdint>

#include "gap_reason.hpp"
#include "record_kind.hpp"

namespace nexigen::canview {

/// @brief Frame-level flags stored in `record_meta` bits 3..6.
enum class RecordFlags : std::uint8_t {
    None = 0U,                   ///< No flags.
    InvalidStruct = 1U << 3U,    ///< Frame failed viewer-local validation.
    TimestampInvalid = 1U << 4U, ///< Hardware timestamp is not usable.
    DuplicateReorder = 1U << 5U, ///< Sequence was equal or moving backwards.
    SequenceReset = 1U << 6U,    ///< Sequence jumped beyond the reset threshold.
};

/// @brief Bitwise OR of two flag sets.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Combined flags.
[[nodiscard]] constexpr RecordFlags operator|(RecordFlags lhs, RecordFlags rhs) noexcept {
    return static_cast<RecordFlags>(static_cast<std::uint8_t>(lhs) |
                                    static_cast<std::uint8_t>(rhs));
}

/// @brief Intersection of two flag sets.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Flags present in both operands.
[[nodiscard]] constexpr RecordFlags operator&(RecordFlags lhs, RecordFlags rhs) noexcept {
    return static_cast<RecordFlags>(static_cast<std::uint8_t>(lhs) &
                                    static_cast<std::uint8_t>(rhs));
}

/// @brief `record_meta` layout shared by frames and synthetic records.
namespace record_meta {

/// @brief Bits 0..2: `CanRecordKind`.
inline constexpr std::uint8_t kind_mask{0x07U};
/// @brief Bits 3..5: `GapReason` for synthetic records.
inline constexpr std::uint8_t gap_reason_mask{0x38U};
/// @brief Bits 3..6: frame flags for frame records.
inline constexpr std::uint8_t frame_flags_mask{0x78U};
/// @brief Bit 6: gap count is exact.
inline constexpr std::uint8_t count_exact_bit{0x40U};

} // namespace record_meta

/// @brief Pack a bare kind.
/// @param kind Kind to store in bits 0..2.
/// @return Packed metadata with all flag bits clear.
[[nodiscard]] constexpr std::uint8_t pack_kind(CanRecordKind kind) noexcept {
    return static_cast<std::uint8_t>(kind) & record_meta::kind_mask;
}

/// @brief Pack a frame or BusIdle heartbeat with its frame flags.
/// @param kind `Frame` or `BusIdleHeartbeat`.
/// @param flags Flags to store in bits 3..6.
/// @return Packed metadata.
[[nodiscard]] constexpr std::uint8_t
pack_frame_meta(CanRecordKind kind, RecordFlags flags = RecordFlags::None) noexcept {
    return static_cast<std::uint8_t>(
        pack_kind(kind) | (static_cast<std::uint8_t>(flags) & record_meta::frame_flags_mask));
}

/// @brief Pack a synthetic loss record with its reason and count-exact bit.
/// @param kind Synthetic kind.
/// @param reason Why the loss happened.
/// @param count_exact Whether the missing count is exact.
/// @return Packed metadata.
[[nodiscard]] constexpr std::uint8_t pack_gap_meta(CanRecordKind kind, GapReason reason,
                                                   bool count_exact) noexcept {
    return static_cast<std::uint8_t>(
        pack_kind(kind) |
        static_cast<std::uint8_t>((static_cast<std::uint8_t>(reason) << 3U) &
                                  record_meta::gap_reason_mask) |
        (count_exact ? record_meta::count_exact_bit : 0U));
}

/// @brief Extract the record kind.
/// @param meta Packed metadata.
/// @return `CanRecordKind` from bits 0..2.
[[nodiscard]] constexpr CanRecordKind meta_kind(std::uint8_t meta) noexcept {
    return static_cast<CanRecordKind>(meta & record_meta::kind_mask);
}

/// @brief Extract frame flags.
/// @param meta Packed metadata.
/// @return Flags from bits 3..6.
[[nodiscard]] constexpr RecordFlags meta_flags(std::uint8_t meta) noexcept {
    return static_cast<RecordFlags>(meta & record_meta::frame_flags_mask);
}

/// @brief Whether a frame flag is set.
/// @param meta Packed metadata.
/// @param flag Flag to test.
/// @return `true` when every bit of `flag` is present.
[[nodiscard]] constexpr bool has_flag(std::uint8_t meta, RecordFlags flag) noexcept {
    const auto bits = static_cast<std::uint8_t>(flag);
    return (meta & bits) == bits;
}

/// @brief Add a frame flag.
/// @param meta Packed metadata.
/// @param flag Flag to set.
/// @return Metadata with the flag set.
[[nodiscard]] constexpr std::uint8_t with_flag(std::uint8_t meta, RecordFlags flag) noexcept {
    return static_cast<std::uint8_t>(meta | static_cast<std::uint8_t>(flag));
}

/// @brief Extract the gap reason from a synthetic record.
/// @param meta Packed metadata.
/// @return `GapReason` from bits 3..5.
[[nodiscard]] constexpr GapReason meta_gap_reason(std::uint8_t meta) noexcept {
    return static_cast<GapReason>((meta & record_meta::gap_reason_mask) >> 3U);
}

/// @brief Whether a synthetic record's count is exact.
/// @param meta Packed metadata.
/// @return Bit 6.
[[nodiscard]] constexpr bool meta_count_exact(std::uint8_t meta) noexcept {
    return (meta & record_meta::count_exact_bit) != 0U;
}

// Every enumerator must fit its field, otherwise packing silently truncates.
static_assert(static_cast<std::uint8_t>(CanRecordKind::DriverDrop) <= record_meta::kind_mask);
static_assert(static_cast<std::uint8_t>(GapReason::SequenceReset) <= 0x07U);

} // namespace nexigen::canview
