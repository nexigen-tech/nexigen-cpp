// Schedule arithmetic, start resolution, and logical continuity tests
// (change add-synchro-event-sync; spec synchro/protocol; tasks 3.1-3.5).
#include "synchro_test_support.hpp"

#include <cstdint>

#include <nexigen/com/loss_info.hpp>
#include <nexigen/com/loss_reason.hpp>
#include <nexigen/synchro/past_target_policy.hpp>
#include <nexigen/synchro/start_mode.hpp>
#include <nexigen/synchro/start_policy.hpp>
#include <nexigen/synchro/sync_schedule.hpp>
#include <nexigen/synchro/sync_tracker.hpp>

namespace {

using namespace nexigen;

com::EventHeader header_with_epoch(std::uint64_t epoch) {
    com::EventHeader header{};
    header.producer_epoch = epoch;
    return header;
}

synchro::SyncPayload position(std::uint64_t cycle, std::uint32_t phase,
                              std::uint32_t phases_total = 10U) {
    synchro::SyncPayload payload{};
    payload.cycle_number = cycle;
    payload.phase_index = phase;
    payload.phases_total = phases_total;
    return payload;
}

com::LossInfo loss(com::LossReason reason, std::uint64_t lost_count = 0U) {
    com::LossInfo info{};
    info.reason = reason;
    info.lost_count = lost_count;
    return info;
}

void test_schedule_grid() {
    const synchro::SyncSchedule schedule{1'000U, 100U, 10U};

    NX_CHECK_EQ(schedule.start_time_ns(), 1'000U);
    NX_CHECK_EQ(schedule.cycle_period_ns(), 100U);
    NX_CHECK_EQ(schedule.phases_per_cycle(), 10U);
    NX_CHECK_EQ(schedule.phase_period_ns(), 10U);

    // T(c, p) = T_start + c * Tc + floor(p * Tc / N).
    NX_CHECK_EQ(schedule.deadline(0U), 1'000U);
    NX_CHECK_EQ(schedule.deadline(1U), 1'010U);
    NX_CHECK_EQ(schedule.deadline(9U), 1'090U);
    NX_CHECK_EQ(schedule.deadline(10U), 1'100U);
    NX_CHECK_EQ(schedule.deadline(19U), 1'190U);
    NX_CHECK_EQ(schedule.deadline(20U), 1'200U);
    NX_CHECK_EQ(schedule.deadline(137U), 1'000U + 13U * 100U + 7U * 10U);

    // The grid is absolute: no deadline is derived from its predecessor.
    for (std::uint64_t slot = 0U; slot < 50U; ++slot) {
        NX_CHECK_EQ(schedule.deadline(slot + 1U) - schedule.deadline(slot), 10U);
    }

    NX_CHECK_EQ(schedule.cycle_start(0U), 1'000U);
    NX_CHECK_EQ(schedule.cycle_start(3U), 1'300U);

    // Truncating phase period for a non-divisible cycle.
    const synchro::SyncSchedule truncated{0U, 10U, 3U};
    NX_CHECK_EQ(truncated.phase_period_ns(), 3U);
    NX_CHECK_EQ(truncated.deadline(1U), 3U);
    NX_CHECK_EQ(truncated.deadline(2U), 6U);
    NX_CHECK_EQ(truncated.deadline(3U), 10U);

    // Empty grids must not divide by zero.
    const synchro::SyncSchedule empty{500U, 0U, 0U};
    NX_CHECK_EQ(empty.phase_period_ns(), 0U);
    NX_CHECK_EQ(empty.deadline(7U), 500U);
}

void test_schedule_boundaries() {
    const synchro::SyncSchedule schedule{1'000U, 100U, 10U};

    NX_CHECK_EQ(schedule.align_up(500U), 1'000U);
    NX_CHECK_EQ(schedule.align_up(1'000U), 1'000U);
    NX_CHECK_EQ(schedule.align_up(1'001U), 1'100U);
    NX_CHECK_EQ(schedule.align_up(1'100U), 1'100U);

    NX_CHECK_EQ(schedule.next_boundary(500U), 1'000U);
    NX_CHECK_EQ(schedule.next_boundary(1'000U), 1'100U);
    NX_CHECK_EQ(schedule.next_boundary(1'001U), 1'100U);
    NX_CHECK_EQ(schedule.next_boundary(1'100U), 1'200U);

    // absolute_phase_at maps an instant onto the grid without state.
    NX_CHECK_EQ(schedule.absolute_phase_at(0U), 0U);
    NX_CHECK_EQ(schedule.absolute_phase_at(1'000U), 0U);
    NX_CHECK_EQ(schedule.absolute_phase_at(1'009U), 0U);
    NX_CHECK_EQ(schedule.absolute_phase_at(1'010U), 1U);
    NX_CHECK_EQ(schedule.absolute_phase_at(1'099U), 9U);
    NX_CHECK_EQ(schedule.absolute_phase_at(1'100U), 10U);
    NX_CHECK_EQ(schedule.absolute_phase_at(1'599U), 59U);

    for (std::uint64_t slot = 0U; slot < 100U; ++slot) {
        const std::uint64_t at = schedule.deadline(slot);
        NX_CHECK_EQ(schedule.absolute_phase_at(at), slot);
        // One nanosecond before the deadline still belongs to the previous slot.
        if (slot != 0U) {
            NX_CHECK_EQ(schedule.absolute_phase_at(at - 1U), slot - 1U);
        }
    }
}

void test_overrun_classification() {
    const synchro::SyncSchedule schedule{1'000U, 100U, 10U};

    // The current slot may still be published late: the next deadline is future.
    auto none = schedule.classify_overrun(0U, 1'005U);
    NX_CHECK_EQ(none.next_phase, 0U);
    NX_CHECK_EQ(none.skipped, 0U);

    none = schedule.classify_overrun(4U, 1'049U);
    NX_CHECK_EQ(none.next_phase, 4U);
    NX_CHECK_EQ(none.skipped, 0U);

    // Exactly at the next deadline the current slot is stale.
    auto single = schedule.classify_overrun(0U, 1'010U);
    NX_CHECK_EQ(single.next_phase, 2U);
    NX_CHECK_EQ(single.skipped, 2U);

    // Several stale slots inside one cycle.
    auto multi = schedule.classify_overrun(2U, 1'035U);
    NX_CHECK_EQ(multi.next_phase, 4U);
    NX_CHECK_EQ(multi.skipped, 2U);

    // A skip that crosses the cycle boundary (slots 8..12 are stale).
    auto crossing = schedule.classify_overrun(8U, 1'125U);
    NX_CHECK_EQ(crossing.next_phase, 13U);
    NX_CHECK_EQ(crossing.skipped, 5U);

    // A whole cycle and more.
    auto whole = schedule.classify_overrun(0U, 1'200U);
    NX_CHECK_EQ(whole.next_phase, 21U);
    NX_CHECK_EQ(whole.skipped, 21U);

    // Every skipped slot is exactly one phase period apart, and the returned
    // slot is the first whose next deadline is strictly in the future.
    NX_CHECK_EQ(schedule.deadline(whole.next_phase), 1'210U);
    NX_CHECK(schedule.deadline(whole.next_phase + 1U) > 1'200U);
    NX_CHECK(schedule.deadline(whole.next_phase) > 1'200U);

    const synchro::SyncSchedule empty{0U, 0U, 0U};
    const auto guarded = empty.classify_overrun(5U, 10U);
    NX_CHECK_EQ(guarded.next_phase, 5U);
    NX_CHECK_EQ(guarded.skipped, 0U);
}

void test_resolve_start_next_cycle() {
    synchro::StartPolicy policy{};
    policy.mode = synchro::StartMode::NextCycle;

    auto resolved = synchro::resolve_start(1'000U, 100U, policy);
    NX_CHECK(resolved.has_value());
    NX_CHECK_EQ(resolved->start_time_ns, 1'100U);
    NX_CHECK(!resolved->realigned);

    // Exactly on a boundary still selects the next boundary.
    resolved = synchro::resolve_start(1'100U, 100U, policy);
    NX_CHECK(resolved.has_value());
    NX_CHECK_EQ(resolved->start_time_ns, 1'200U);

    // A zero-anchored grid ignores any phase offset of the caller.
    resolved = synchro::resolve_start(1'250U, 100U, policy);
    NX_CHECK(resolved.has_value());
    NX_CHECK_EQ(resolved->start_time_ns, 1'300U);

    NX_CHECK(!synchro::resolve_start(1'000U, 0U, policy).has_value());
}

void test_resolve_start_at_target() {
    synchro::StartPolicy policy{};
    policy.mode = synchro::StartMode::AtTarget;

    // Future exact target.
    policy.target_time_ns = 1'150U;
    policy.align_target_to_cycle = false;
    auto resolved = synchro::resolve_start(1'000U, 100U, policy);
    NX_CHECK(resolved.has_value());
    NX_CHECK_EQ(resolved->start_time_ns, 1'150U);
    NX_CHECK(!resolved->realigned);

    // Future target snapped up to the cycle grid.
    policy.align_target_to_cycle = true;
    resolved = synchro::resolve_start(1'000U, 100U, policy);
    NX_CHECK(resolved.has_value());
    NX_CHECK_EQ(resolved->start_time_ns, 1'200U);

    // A target already on the grid stays there.
    policy.target_time_ns = 1'200U;
    resolved = synchro::resolve_start(1'000U, 100U, policy);
    NX_CHECK(resolved.has_value());
    NX_CHECK_EQ(resolved->start_time_ns, 1'200U);

    // Past target rejected.
    policy.target_time_ns = 900U;
    policy.past_target = synchro::PastTargetPolicy::Reject;
    NX_CHECK(!synchro::resolve_start(1'000U, 100U, policy).has_value());

    // Past target realigned to the next boundary.
    policy.past_target = synchro::PastTargetPolicy::RealignToNextCycle;
    resolved = synchro::resolve_start(1'000U, 100U, policy);
    NX_CHECK(resolved.has_value());
    NX_CHECK_EQ(resolved->start_time_ns, 1'100U);
    NX_CHECK(resolved->realigned);

    // A one-shot override wins over the stored policy target.
    policy.target_time_ns = 900U;
    policy.past_target = synchro::PastTargetPolicy::Reject;
    resolved = synchro::resolve_start(1'000U, 100U, policy, true, 1'250U);
    NX_CHECK(resolved.has_value());
    NX_CHECK_EQ(resolved->start_time_ns, 1'300U);

    // Invalid vocabulary is rejected.
    policy.mode = static_cast<synchro::StartMode>(42);
    NX_CHECK(!synchro::resolve_start(1'000U, 100U, policy).has_value());

    policy.mode = synchro::StartMode::AtTarget;
    policy.past_target = static_cast<synchro::PastTargetPolicy>(42);
    NX_CHECK(!synchro::resolve_start(1'000U, 100U, policy).has_value());
}

void test_tracker_first_position() {
    synchro::SyncTracker tracker{};
    NX_CHECK(!tracker.has_position());

    const auto observation = tracker.observe(header_with_epoch(7U), position(3U, 4U, 10U));
    NX_CHECK(observation.first);
    NX_CHECK(!observation.resync);
    NX_CHECK(!observation.epoch_changed);
    NX_CHECK(!observation.phases_changed);
    NX_CHECK_EQ(observation.absolute_phase, 3U * 10U + 4U);
    NX_CHECK_EQ(observation.generator_skips, 0U);
    NX_CHECK(tracker.has_position());
    NX_CHECK_EQ(tracker.absolute_phase(), 34U);
    NX_CHECK_EQ(tracker.epoch(), 7U);
    NX_CHECK_EQ(tracker.phases_total(), 10U);
}

void test_tracker_sequential_and_cycle_boundary() {
    synchro::SyncTracker tracker{};
    const auto header = header_with_epoch(1U);

    NX_CHECK(tracker.observe(header, position(0U, 0U)).first);
    for (std::uint32_t phase = 1U; phase < 10U; ++phase) {
        const auto observation = tracker.observe(header, position(0U, phase));
        NX_CHECK(!observation.resync);
        NX_CHECK_EQ(observation.generator_skips, 0U);
        NX_CHECK_EQ(observation.absolute_phase, phase);
    }
    // Cycle boundary: phase 0 of cycle 1 is absolute 10, never a skip.
    auto observation = tracker.observe(header, position(1U, 0U));
    NX_CHECK(!observation.resync);
    NX_CHECK_EQ(observation.generator_skips, 0U);
    NX_CHECK_EQ(observation.absolute_phase, 10U);

    observation = tracker.observe(header, position(2U, 5U));
    NX_CHECK(!observation.resync);
    NX_CHECK_EQ(observation.absolute_phase, 25U);
    NX_CHECK_EQ(observation.generator_skips, 14U);
}

void test_tracker_generator_skips() {
    synchro::SyncTracker tracker{};
    const auto header = header_with_epoch(1U);

    static_cast<void>(tracker.observe(header, position(0U, 1U))); // absolute 1
    // The generator skipped phase 2 and 3: no transport loss was reported.
    const auto observation = tracker.observe(header, position(0U, 4U));
    NX_CHECK(!observation.resync);
    NX_CHECK_EQ(observation.generator_skips, 2U);
}

void test_tracker_transport_gap_reconciliation() {
    synchro::SyncTracker tracker{};
    const auto header = header_with_epoch(1U);

    static_cast<void>(tracker.observe(header, position(0U, 1U))); // absolute 1
    tracker.note_loss(loss(com::LossReason::TransportGap, 2U));
    // Two transport samples were lost, so the logical jump of 3 is explained
    // entirely by transport loss: no generator skip.
    auto observation = tracker.observe(header, position(0U, 4U));
    NX_CHECK(!observation.resync);
    NX_CHECK_EQ(observation.generator_skips, 0U);

    // A transport gap plus a real generator skip: jump 4 with 2 lost samples
    // leaves exactly one skipped phase.
    tracker.note_loss(loss(com::LossReason::TransportGap, 2U));
    observation = tracker.observe(header, position(0U, 8U)); // absolute 8
    NX_CHECK(!observation.resync);
    NX_CHECK_EQ(observation.generator_skips, 1U);
}

void test_tracker_resync_causes() {
    const auto header = header_with_epoch(1U);

    // Producer restart.
    {
        synchro::SyncTracker tracker{};
        static_cast<void>(tracker.observe(header, position(5U, 5U)));
        tracker.note_loss(loss(com::LossReason::ProducerRestart));
        const auto observation = tracker.observe(header_with_epoch(2U), position(0U, 0U));
        NX_CHECK(observation.resync);
        NX_CHECK(observation.epoch_changed);
        NX_CHECK_EQ(observation.generator_skips, 0U);
    }

    // Epoch change without a loss callback.
    {
        synchro::SyncTracker tracker{};
        static_cast<void>(tracker.observe(header, position(5U, 5U)));
        const auto observation = tracker.observe(header_with_epoch(2U), position(5U, 6U));
        NX_CHECK(observation.resync);
        NX_CHECK(observation.epoch_changed);
        NX_CHECK_EQ(observation.generator_skips, 0U);
    }

    // Unexpected sequence reset.
    {
        synchro::SyncTracker tracker{};
        static_cast<void>(tracker.observe(header, position(2U, 3U)));
        tracker.note_loss(loss(com::LossReason::UnexpectedSequenceReset));
        const auto observation = tracker.observe(header, position(2U, 3U));
        NX_CHECK(observation.resync);
        NX_CHECK_EQ(observation.generator_skips, 0U);
    }

    // Phase count change.
    {
        synchro::SyncTracker tracker{};
        static_cast<void>(tracker.observe(header, position(0U, 9U, 10U)));
        const auto observation = tracker.observe(header, position(1U, 0U, 20U));
        NX_CHECK(observation.resync);
        NX_CHECK(observation.phases_changed);
    }

    // Backward cycle.
    {
        synchro::SyncTracker tracker{};
        static_cast<void>(tracker.observe(header, position(3U, 0U)));
        const auto observation = tracker.observe(header, position(2U, 9U));
        NX_CHECK(observation.resync);
    }

    // Out-of-order absolute position inside one cycle.
    {
        synchro::SyncTracker tracker{};
        static_cast<void>(tracker.observe(header, position(0U, 7U)));
        const auto observation = tracker.observe(header, position(0U, 5U));
        NX_CHECK(observation.resync);
    }
}

void test_tracker_duplicate_and_reset() {
    synchro::SyncTracker tracker{};
    const auto header = header_with_epoch(1U);

    static_cast<void>(tracker.observe(header, position(0U, 0U)));
    // A duplicate notification changes nothing: the transport never delivers
    // the duplicate sample itself.
    tracker.note_loss(loss(com::LossReason::Duplicate));
    auto observation = tracker.observe(header, position(0U, 1U));
    NX_CHECK(!observation.resync);
    NX_CHECK_EQ(observation.generator_skips, 0U);

    tracker.reset();
    NX_CHECK(!tracker.has_position());
    NX_CHECK_EQ(tracker.absolute_phase(), 0U);
    NX_CHECK_EQ(tracker.epoch(), 0U);
    NX_CHECK_EQ(tracker.phases_total(), 0U);
    observation = tracker.observe(header, position(9U, 9U));
    NX_CHECK(observation.first);
    NX_CHECK(!observation.resync);
}

} // namespace

void run_schedule_tracker_suite() {
    run_suite("schedule grid", test_schedule_grid);
    run_suite("schedule boundaries", test_schedule_boundaries);
    run_suite("overrun classification", test_overrun_classification);
    run_suite("resolve next-cycle start", test_resolve_start_next_cycle);
    run_suite("resolve target start", test_resolve_start_at_target);
    run_suite("tracker first position", test_tracker_first_position);
    run_suite("tracker sequential", test_tracker_sequential_and_cycle_boundary);
    run_suite("tracker generator skips", test_tracker_generator_skips);
    run_suite("tracker transport gaps", test_tracker_transport_gap_reconciliation);
    run_suite("tracker resync causes", test_tracker_resync_causes);
    run_suite("tracker duplicate/reset", test_tracker_duplicate_and_reset);
}
