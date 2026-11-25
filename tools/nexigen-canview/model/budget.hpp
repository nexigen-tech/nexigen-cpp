// nexigen-canview — memory ledger and degraded-mode planning.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace nexigen::canview {

/// @brief Preallocated viewer state derived from `max_memory_mb`.
struct MemoryPlan {
    std::uint64_t max_memory_mb{0};    ///< Configured model budget.
    std::size_t ring_capacity{0};      ///< History slots (power of two).
    std::size_t live_views{0};         ///< Maximum LIVE views including `All`.
    std::size_t paused_snapshots{0};   ///< Maximum PAUSED snapshots.
    std::uint64_t paused_bytes{0};     ///< Reserved PAUSED snapshot budget.
    std::size_t overwrite_rows{0};     ///< Overwrite table row capacity.
    std::size_t heatmap_columns{0};    ///< Heatmap column cap.
    std::size_t export_queue_slots{0}; ///< Async export queue slots.
    bool eds_enabled{false};           ///< Whether EDS loading is available.
    std::uint64_t eds_budget_mb{0};    ///< Separate EDS budget.
    bool degraded{false};              ///< Documented 16 MiB degraded mode.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the derived capacities.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Fixed preallocated bytes outside the ring (`D11`, 43 MiB).
inline constexpr std::uint64_t fixed_reserve_bytes{43ULL * 1024ULL * 1024ULL};

/// @brief Additional reserve subtracted by the capacity formula (`D11`, 8 MiB).
inline constexpr std::uint64_t capacity_extra_reserve_bytes{8ULL * 1024ULL * 1024ULL};

/// @brief Worst-case bytes per slot including eight visible indexes (164 B).
inline constexpr std::uint64_t worst_case_bytes_per_slot{164ULL};

/// @brief Minimum history capacity.
inline constexpr std::size_t minimum_capacity{1024U};

/// @brief History capacity for a budget in MiB.
///
/// `bit_floor(max(1024, (max_memory_mb - 43 MiB - 8 MiB) / 164))`. The two
/// reserves are the documented worst-case ledger, so the result preserves
/// 1 048 576 records at 256 MiB and 2 097 152 at 384 MiB.
///
/// @param max_memory_mb Model budget in MiB.
/// @return Power-of-two slot count, at least 1024.
[[nodiscard]] std::size_t capacity_for_budget(std::uint64_t max_memory_mb) noexcept;

/// @brief Derive the full preallocation plan.
/// @param max_memory_mb Model budget in MiB.
/// @return Capacities for the ring, views, overwrite, heatmap and EDS.
[[nodiscard]] MemoryPlan plan_for_budget(std::uint64_t max_memory_mb) noexcept;

} // namespace nexigen::canview
