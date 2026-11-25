#include "budget.hpp"

#include <algorithm>
#include <bit>
#include <format>

namespace nexigen::canview {

std::size_t capacity_for_budget(std::uint64_t max_memory_mb) noexcept {
    const std::uint64_t budget_bytes = max_memory_mb * 1024ULL * 1024ULL;
    const std::uint64_t reserve = fixed_reserve_bytes + capacity_extra_reserve_bytes;
    const std::uint64_t available = budget_bytes > reserve ? budget_bytes - reserve : 0ULL;
    std::uint64_t capacity = available / worst_case_bytes_per_slot;
    if (capacity < minimum_capacity) {
        capacity = minimum_capacity;
    }
    return std::bit_floor(capacity);
}

MemoryPlan plan_for_budget(std::uint64_t max_memory_mb) noexcept {
    MemoryPlan plan{};
    plan.max_memory_mb = max_memory_mb;
    plan.ring_capacity = capacity_for_budget(max_memory_mb);
    plan.export_queue_slots = 4096U;
    plan.live_views = 8U;

    // Sixteen MiB is the documented degraded configuration: one optional
    // snapshot, a reduced overwrite table, a minimal heatmap, no EDS, and
    // at least 1024 history records.
    plan.degraded = max_memory_mb <= 16ULL;
    if (plan.degraded) {
        plan.paused_snapshots = 1U;
        plan.paused_bytes = 1ULL * 1024ULL * 1024ULL;
        plan.overwrite_rows = 512U;
        plan.heatmap_columns = 256U;
        plan.eds_enabled = false;
        plan.eds_budget_mb = 0ULL;
        return plan;
    }

    plan.paused_snapshots = 16U;
    plan.paused_bytes = 18ULL * 1024ULL * 1024ULL;
    plan.overwrite_rows = 4096U;
    plan.heatmap_columns = 2048U;
    plan.eds_enabled = true;
    plan.eds_budget_mb = 32ULL;
    return plan;
}

std::string MemoryPlan::to_string() const {
    return std::format(
        "MemoryPlan(max_mb={}, ring={}, live_views={}, paused={}, overwrite={}, heatmap={}, "
        "export={}, eds={}, eds_mb={}, degraded={})",
        max_memory_mb, ring_capacity, live_views, paused_snapshots, overwrite_rows, heatmap_columns,
        export_queue_slots, eds_enabled, eds_budget_mb, degraded);
}

} // namespace nexigen::canview
