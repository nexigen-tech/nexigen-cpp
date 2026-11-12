// nexigen::logging — snapshot of dropped-record accounting (spec
// logging/multi-producer: the three drop causes are counted separately;
// third threads may read it anytime as a cross-shard aggregate).
#pragma once

#include <cstdint>
#include <string>

namespace nexigen::logging {

/// @brief Snapshot of dropped-record accounting returned by `dropped()`.
///
/// The three drop causes are counted independently by the engine, and a
/// snapshot aggregates every shard. Reading it from any thread is safe, but the
/// result is a best-effort snapshot rather than a linearized total: concurrent
/// owners may commit further drops while the aggregate is assembled.
///
/// @note Spec `logging/multi-producer`, requirement "Sharded drop accounting without contention".
struct DroppedStats {
    /// @brief Rejected by the per-shard throttle window (design D9).
    std::uint64_t rate_limited{};
    /// @brief Rejected by a full shard ring.
    std::uint64_t buffer_full{};
    /// @brief Dropped because the thread had no free shard slot (design D6).
    std::uint64_t slot_exhausted{};

    /// @brief Sum of the three per-cause counters.
    ///
    /// @return `rate_limited + buffer_full + slot_exhausted`.
    /// @note `constexpr` and `noexcept`, so it is usable in constant expressions
    ///       and on log-free code paths.
    [[nodiscard]] constexpr std::uint64_t total() const noexcept {
        return rate_limited + buffer_full + slot_exhausted;
    }

    /// @brief Cold-path diagnostic rendering of the per-cause counts and their total.
    ///
    /// @return Text of the form `DroppedStats(rate_limited=<n>, buffer_full=<n>,
    ///         slot_exhausted=<n>, total=<n>)`.
    /// @note Project mandate 9 (`to_string` on every public value type).
    [[nodiscard]] std::string to_string() const {
        return "DroppedStats(rate_limited=" + std::to_string(rate_limited) +
               ", buffer_full=" + std::to_string(buffer_full) +
               ", slot_exhausted=" + std::to_string(slot_exhausted) +
               ", total=" + std::to_string(total()) + ")";
    }
};

/// @brief Free `to_string` entry point for a drop snapshot (mandate 9).
///
/// @param stats Snapshot to render.
/// @return Exactly `stats.to_string()`.
/// @note Project mandate 9; ADL-friendly mirror of the member function.
[[nodiscard]] inline std::string to_string(const DroppedStats& stats) {
    return stats.to_string();
}

} // namespace nexigen::logging
