// nexigen-canview — per-channel UTC anchor estimation.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <nexigen/com/event_header.hpp>
#include <nexigen/hw/can/can_message.hpp>

#include "can_ingress_record.hpp"

namespace nexigen::canview {

/// @brief UTC lookup seam used by the estimator.
///
/// The default lookup calls `chrono::TimeBaseClock::utc_from_canonical`;
/// tests inject a deterministic clock through the context pointer.
using UtcLookup = std::optional<std::int64_t> (*)(std::int64_t canonical_ns, void* context);

/// @brief Default lookup backed by the process time base.
/// @param canonical_ns Host `CLOCK_MONOTONIC_RAW` instant.
/// @param context Unused.
/// @return Estimated UTC nanoseconds, or `std::nullopt` when uncalibrated.
[[nodiscard]] std::optional<std::int64_t> default_utc_lookup(std::int64_t canonical_ns,
                                                             void* context) noexcept;

/// @brief Estimate a UTC offset and drift for one CAN channel.
///
/// Each observation pairs the publisher's ingest timestamp (host monotonic
/// raw) with the frame's hardware timestamp. The estimator keeps a short
/// window, publishes the minimum-latency offset, derives drift from the oldest
/// and newest samples, and starts a new epoch on a material jump or a timestamp
/// source change. It never fabricates UTC: without a time base the observation
/// returns no anchor event.
///
/// The capture thread owns one instance per live channel. The class is
/// allocation-free after construction.
class AnchorEstimator {
public:
    /// @brief Number of samples kept for the offset/drift window.
    static constexpr std::size_t window_size{16U};
    /// @brief Offset change that starts a new epoch (100 ms).
    static constexpr std::int64_t material_jump_ns{100'000'000};
    /// @brief Minimum offset change worth publishing (1 us).
    static constexpr std::int64_t publish_resolution_ns{1'000};
    /// @brief Periodic anchor refresh interval.
    static constexpr std::uint64_t publish_period_ns{1'000'000'000ULL};

    /// @brief Construct an estimator for one channel.
    /// @param channel_id Channel identity copied into every anchor event.
    /// @param lookup UTC lookup seam; defaults to the process time base.
    /// @param context Opaque context passed to `lookup`.
    explicit AnchorEstimator(std::uint16_t channel_id = 0, UtcLookup lookup = &default_utc_lookup,
                             void* context = nullptr) noexcept
        : channel_id_(channel_id), lookup_(lookup), context_(context) {}

    /// @brief Observe one delivered frame.
    /// @param header Transport header carrying the publisher ingest time.
    /// @param message Delivered CAN message.
    /// @return An anchor event to enqueue, or `std::nullopt` when the current
    ///         anchor is still fresh or UTC is unavailable.
    [[nodiscard]] std::optional<AnchorEvent>
    observe(const nexigen::com::EventHeader& header,
            const nexigen::hw::can::CanMessage& message) noexcept;

    /// @brief Current anchor epoch.
    /// @return Epoch index, starting at zero.
    [[nodiscard]] std::uint16_t epoch() const noexcept { return epoch_; }

    /// @brief Current anchor source (`CanTimestampSource` value).
    /// @return Source byte stored in records.
    [[nodiscard]] std::uint8_t source() const noexcept { return source_; }

    /// @brief Whether at least one UTC sample has been accepted.
    /// @return `true` after a successful lookup.
    [[nodiscard]] bool has_anchor() const noexcept { return has_anchor_; }

    /// @brief Last published UTC offset.
    /// @return UTC minus hardware time in nanoseconds.
    [[nodiscard]] std::int64_t utc_offset_ns() const noexcept { return published_offset_ns_; }

    /// @brief Last published drift estimate.
    /// @return Drift in parts per billion.
    [[nodiscard]] std::int32_t drift_ppb() const noexcept { return drift_ppb_; }

private:
    /// @brief One accepted offset sample.
    struct Sample {
        std::int64_t offset_ns{0};    ///< UTC minus hardware time.
        std::uint64_t hardware_ns{0}; ///< Hardware timestamp.
    };

    /// @brief Clear the sample window.
    void clear_window() noexcept;
    /// @brief Add one sample to the window.
    /// @param sample Sample to store.
    void add_sample(Sample sample) noexcept;
    /// @brief Minimum offset in the window.
    /// @return Offset or zero when empty.
    [[nodiscard]] std::int64_t minimum_offset() const noexcept;
    /// @brief Recompute drift from the window endpoints.
    /// @return Drift in ppb.
    [[nodiscard]] std::int32_t compute_drift() const noexcept;

    std::uint16_t channel_id_{0};              ///< Channel identity.
    UtcLookup lookup_{&default_utc_lookup};    ///< UTC lookup.
    void* context_{nullptr};                   ///< Lookup context.
    std::array<Sample, window_size> window_{}; ///< Offset window ring.
    std::size_t window_head_{0};               ///< Oldest sample index.
    std::size_t window_count_{0};              ///< Number of valid samples.
    std::uint16_t epoch_{0};                   ///< Current epoch.
    std::uint8_t source_{0};                   ///< Current timestamp source.
    bool has_anchor_{false};                   ///< A sample was accepted.
    bool source_known_{false};                 ///< `source_` has been set.
    std::int64_t published_offset_ns_{0};      ///< Last published offset.
    std::int32_t drift_ppb_{0};                ///< Last published drift.
    std::uint64_t last_publish_hw_ns_{0};      ///< Hardware time at last publish.
    std::uint64_t first_hw_ns_{0};             ///< First hardware time of the epoch.
    std::int64_t first_offset_ns_{0};          ///< First offset of the epoch.
};

} // namespace nexigen::canview
