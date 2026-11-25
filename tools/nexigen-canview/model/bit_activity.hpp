// nexigen-canview — bounded bit-activity matrix.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <nexigen/hw/can/can_message.hpp>

#include "record_source.hpp"

namespace nexigen::canview {

/// @brief One bit cell of the activity matrix.
struct BitActivityCell {
    std::uint32_t transitions{0}; ///< 0->1 and 1->0 transitions.
    std::uint32_t frames{0};      ///< Frames contributing to the cell.
    double frequency{0.0};        ///< Transitions per second over the window.
};

/// @brief Last 4 096 frames or 10 s of one key, whichever is shorter.
class BitActivity {
public:
    /// @brief Maximum frames retained.
    static constexpr std::size_t max_frames{4096U};
    /// @brief Maximum time window.
    static constexpr std::uint64_t window_ns{10'000'000'000ULL};
    /// @brief Maximum payload bytes.
    static constexpr std::size_t max_bytes{64U};

    /// @brief Clear the matrix.
    void init() noexcept;

    /// @brief Rebuild from one key.
    /// @param source History to scan.
    /// @param channel_id Channel identity.
    /// @param can_id Identifier.
    /// @param extended Whether the key uses a 29-bit identifier.
    void build(const RecordSource& source, std::uint16_t channel_id, std::uint32_t can_id,
               bool extended);

    /// @brief Whether at least one frame was found.
    /// @return Availability.
    [[nodiscard]] bool available() const noexcept { return available_; }
    /// @brief Whether fewer than two frames were found.
    /// @return Low-confidence flag.
    [[nodiscard]] bool low_confidence() const noexcept { return low_confidence_; }
    /// @brief Matrix byte rows (8 for classic, 64 for FD).
    /// @return Row count.
    [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
    /// @brief Frames retained.
    /// @return Frame count.
    [[nodiscard]] std::uint32_t frame_count() const noexcept { return frame_count_; }
    /// @brief Covered time span.
    /// @return Nanoseconds.
    [[nodiscard]] std::uint64_t span_ns() const noexcept { return span_ns_; }
    /// @brief One matrix cell.
    /// @param byte Byte index.
    /// @param bit Bit index.
    /// @return Cell; an out-of-range index returns a zero cell.
    [[nodiscard]] const BitActivityCell& at(std::size_t byte, std::size_t bit) const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with frame count and span.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Collect matching frames newest-to-oldest.
    void collect(const RecordSource& source, std::uint16_t channel_id, std::uint32_t can_id,
                 bool extended);
    /// @brief Compute dimensions, span and transition cells.
    void finalize() noexcept;
    /// @brief Whether one record matches the key.
    [[nodiscard]] static bool matches(const CanRecordView& record, std::uint16_t channel_id,
                                      std::uint32_t can_id, bool extended) noexcept;
    /// @brief Count transitions between two adjacent frames.
    void count_transitions(const nexigen::hw::can::CanMessage& previous,
                           const nexigen::hw::can::CanMessage& current) noexcept;

    std::array<std::array<BitActivityCell, 8>, max_bytes> matrix_{}; ///< Matrix.
    std::vector<nexigen::hw::can::CanMessage> frames_{};             ///< Retained frames.
    std::size_t bytes_{8U};                                          ///< Byte rows.
    std::uint32_t frame_count_{0};                                   ///< Frames retained.
    std::uint64_t span_ns_{0};                                       ///< Newest minus oldest.
    bool available_{false};                                          ///< Any frame found.
    bool low_confidence_{true};                                      ///< Fewer than two frames.
};

} // namespace nexigen::canview
