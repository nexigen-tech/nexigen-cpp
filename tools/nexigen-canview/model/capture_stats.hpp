// nexigen-canview — COM-free capture counters.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace nexigen::canview {

/// @brief Snapshot of the capture thread's counters.
struct CaptureStats {
    std::uint64_t received{0};         ///< Valid frames accepted.
    std::uint64_t bus_idle{0};         ///< Bus-idle heartbeats accepted.
    std::uint64_t synthetic{0};        ///< Loss markers accepted.
    std::uint64_t overflow{0};         ///< Items the ingress queue rejected.
    std::uint64_t mismatched{0};       ///< Frames on the wrong channel binding.
    std::uint64_t duplicates{0};       ///< Duplicate/reorder observations.
    std::uint64_t resets{0};           ///< Sequence resets.
    std::uint64_t hardware_gaps{0};    ///< Hardware-sequence gaps.
    std::uint64_t transport_losses{0}; ///< `com::LossInfo` losses.
    std::uint64_t ingress_losses{0};   ///< Ingress queue overflows.
    std::uint64_t driver_drops{0};     ///< Driver-reported drops.
    std::uint32_t active_channels{0};  ///< Channels that started.
    std::uint32_t failed_channels{0};  ///< Channels that failed to start.
    std::size_t ingress_size{0};       ///< Current ingress occupancy.
    int last_error{0};                 ///< Last transport error, or zero.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the counters.
    [[nodiscard]] std::string to_string() const;
};

} // namespace nexigen::canview
