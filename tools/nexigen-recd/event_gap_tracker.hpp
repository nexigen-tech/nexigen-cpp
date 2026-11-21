// nexigen-recd — event sequence/epoch gap tracking (change
// record-telemetry-sessions; spec recorder/telemetry-capture "Event channel
// capture").
#pragma once

#include <cstdint>
#include <optional>

namespace nexigen::recorder {

/// @brief One missing range reported by an event stream.
struct EventGap {
    std::uint64_t first_lost_sequence{0};
    std::uint64_t lost_count{0};
};

/// @brief Per-publisher event sequence tracker.
///
/// A first sample or a new producer epoch starts a fresh baseline; a sequence
/// skip within one epoch reports the missing range.
class EventGapTracker {
public:
    /// @brief Observe one event header.
    ///
    /// @param epoch Producer epoch.
    /// @param sequence Transport sequence.
    /// @return The missing range, or `std::nullopt` when the sample is
    ///         contiguous or starts a baseline.
    [[nodiscard]] std::optional<EventGap> observe(std::uint64_t epoch,
                                                  std::uint64_t sequence) noexcept {
        if (!valid_ || epoch != epoch_) {
            valid_ = true;
            epoch_ = epoch;
            last_sequence_ = sequence;
            return std::nullopt;
        }
        if (sequence > last_sequence_ + 1U) {
            EventGap gap{};
            gap.first_lost_sequence = last_sequence_ + 1U;
            gap.lost_count = sequence - last_sequence_ - 1U;
            last_sequence_ = sequence;
            return gap;
        }
        if (sequence > last_sequence_) {
            last_sequence_ = sequence;
        }
        return std::nullopt;
    }

private:
    std::uint64_t epoch_{0};
    std::uint64_t last_sequence_{0};
    bool valid_{false};
};

} // namespace nexigen::recorder
