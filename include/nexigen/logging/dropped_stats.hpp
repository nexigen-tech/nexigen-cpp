// nexigen::logging — snapshot of dropped-record accounting (spec
// logging/multi-producer: the three drop causes are counted separately;
// third threads may read it anytime as a cross-shard aggregate).
#pragma once

#include <cstdint>
#include <string>

namespace nexigen::logging {

/// Snapshot of dropped-record accounting.
struct DroppedStats {
    std::uint64_t rate_limited{};   ///< Rejected by the throttle window (D9).
    std::uint64_t buffer_full{};    ///< Rejected by a full shard ring.
    std::uint64_t slot_exhausted{}; ///< Dropped: no free shard slot (D6).

    [[nodiscard]] constexpr std::uint64_t total() const noexcept {
        return rate_limited + buffer_full + slot_exhausted;
    }

    /// Cold-path diagnostic (mandate 9): per-cause counts + total.
    [[nodiscard]] std::string to_string() const {
        return "DroppedStats(rate_limited=" + std::to_string(rate_limited) +
               ", buffer_full=" + std::to_string(buffer_full) +
               ", slot_exhausted=" + std::to_string(slot_exhausted) +
               ", total=" + std::to_string(total()) + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const DroppedStats& stats) {
    return stats.to_string();
}

} // namespace nexigen::logging
