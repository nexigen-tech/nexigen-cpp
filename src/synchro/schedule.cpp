// nexigen::synchro — start-instant resolution on the absolute phase grid
// (change add-synchro-event-sync; spec synchro/protocol; design D4).
#include <cstdint>

#include <nexigen/synchro/past_target_policy.hpp>
#include <nexigen/synchro/start_mode.hpp>
#include <nexigen/synchro/sync_error.hpp>
#include <nexigen/synchro/sync_schedule.hpp>

namespace nexigen::synchro {

namespace {

/// @brief First grid boundary strictly after `now`, anchored at zero.
/// @param now Current raw instant.
/// @param cycle_period Cycle period; must be positive.
/// @return Boundary `k * Tc` with `k * Tc > now`.
[[nodiscard]] constexpr std::uint64_t
next_boundary_after(std::uint64_t now, std::uint64_t cycle_period) noexcept {
    const std::uint64_t boundary = (now / cycle_period) * cycle_period;
    return boundary <= now ? boundary + cycle_period : boundary;
}

/// @brief First grid boundary at or after `target`, anchored at zero.
/// @param target Instant to align.
/// @param cycle_period Cycle period; must be positive.
/// @return Boundary `k * Tc` with `k * Tc >= target`.
[[nodiscard]] constexpr std::uint64_t
align_boundary_up(std::uint64_t target, std::uint64_t cycle_period) noexcept {
    const std::uint64_t boundary = (target / cycle_period) * cycle_period;
    return boundary < target ? boundary + cycle_period : boundary;
}

} // namespace

SyncResult<StartResolution> resolve_start(std::uint64_t now_ns, std::uint64_t cycle_period_ns,
                                         const StartPolicy& policy, bool has_target_override,
                                         std::uint64_t target_override_ns) noexcept {
    if (cycle_period_ns == 0U) {
        return std::unexpected(make_config_error());
    }
    if (!policy.validate().has_value()) {
        return std::unexpected(make_config_error());
    }

    StartResolution resolved{};
    if (policy.mode == StartMode::NextCycle) {
        resolved.start_time_ns = next_boundary_after(now_ns, cycle_period_ns);
        return resolved;
    }

    const std::uint64_t target = has_target_override ? target_override_ns : policy.target_time_ns;
    if (target > now_ns) {
        resolved.start_time_ns = policy.align_target_to_cycle
                                     ? align_boundary_up(target, cycle_period_ns)
                                     : target;
        return resolved;
    }

    if (policy.past_target == PastTargetPolicy::Reject) {
        return std::unexpected(make_config_error());
    }
    resolved.start_time_ns = next_boundary_after(now_ns, cycle_period_ns);
    resolved.realigned = true;
    return resolved;
}

} // namespace nexigen::synchro
