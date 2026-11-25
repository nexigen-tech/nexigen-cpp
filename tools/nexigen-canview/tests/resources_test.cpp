// Resource guard, allocation failure seams and burst-queue tests.
#include <cstdint>
#include <memory>

#include "budget.hpp"
#include "can_record_ring.hpp"
#include "can_view_model.hpp"
#include "can_queues.hpp"
#include "heatmap.hpp"
#include "overwrite_table.hpp"
#include "resource_guard.hpp"
#include "suite.hpp"
#include "support/check.hpp"

namespace {

namespace canview = nexigen::canview;

void test_resource_guard() {
    canview::ResourceGuard guard;
    NX_CHECK(guard.mode() == canview::ResourceMode::Normal);
    NX_CHECK(!guard.frame_interval_seconds());
    static_cast<void>(guard.observe(0U, 0U, 0U));
    std::uint64_t cpu = 0U;
    for (std::uint64_t window = 1U; window <= 3U; ++window) {
        cpu += 600'000'000ULL;
        static_cast<void>(guard.observe(window * 1'000'000'000ULL, cpu, 0U));
    }
    NX_CHECK(guard.mode() == canview::ResourceMode::Eco);
    NX_CHECK(guard.trigger() == canview::EcoTrigger::Cpu);
    NX_CHECK(guard.frame_interval_seconds() > 0.0);
    const std::uint64_t eco_time = 3'000'000'000ULL;
    NX_CHECK(guard.recompute_due(eco_time));
    NX_CHECK(!guard.recompute_due(eco_time + 100'000'000ULL));
    guard.note_input();
    NX_CHECK(guard.recompute_due(eco_time + 110'000'000ULL));
    for (std::uint64_t window = 4U; window <= 5U; ++window) {
        cpu += 100'000'000ULL;
        static_cast<void>(guard.observe(window * 1'000'000'000ULL, cpu, 0U));
    }
    NX_CHECK(guard.mode() == canview::ResourceMode::Normal);

    canview::ResourceGuard storm;
    static_cast<void>(storm.observe(0U, 0U, 0U));
    static_cast<void>(storm.observe(1'000'000'000ULL, 0U, 200'000U));
    NX_CHECK(storm.mode() == canview::ResourceMode::Eco);
    NX_CHECK(storm.trigger() == canview::EcoTrigger::Storm);
}

void test_allocation_failures() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    canview::CanViewModel model{*ring};
    model.fail_view_allocation_for_tests(true);
    const auto rejected =
        model.create_view(canview::ViewKind::All, 0U, 0U, "fail", canview::ViewDefaults{});
    NX_CHECK(!rejected);
    if (!rejected) {
        NX_CHECK(rejected.error() == nexigen::core::ErrorCode::ResourceExhausted);
    }
    model.fail_view_allocation_for_tests(false);
    const auto view =
        model.create_view(canview::ViewKind::All, 0U, 0U, "ok", canview::ViewDefaults{});
    NX_CHECK(view.has_value());
    if (!view) {
        return;
    }
    model.fail_snapshot_allocation_for_tests(true);
    const auto no_snapshot =
        model.clone_view(*view, true, "paused", canview::CloneOptions{}, canview::ViewDefaults{});
    NX_CHECK(!no_snapshot);
    if (!no_snapshot) {
        NX_CHECK(no_snapshot.error() == nexigen::core::ErrorCode::ResourceExhausted);
    }

    canview::OverwriteTable table;
    table.init(4U);
    table.disable_for_tests();
    NX_CHECK(table.rows().empty());

    canview::HeatmapWindow heatmap;
    heatmap.init();
    heatmap.fail_allocation_for_tests(true);
    canview::RingRecordSource source{*ring};
    heatmap.rebuild(source, nullptr, canview::HeatmapAxis::SharedHardware, 0U, 1'000U, 1U, {});
    NX_CHECK(heatmap.column_count() == 0U);

    const auto degraded = canview::plan_for_budget(16U);
    NX_CHECK(degraded.degraded);
    NX_CHECK(degraded.ring_capacity >= 1024U);
}

void test_burst_queue() {
    auto queue = std::make_unique<canview::IngressQueue>();
    canview::CanIngressRecord record{};
    record.kind = canview::CanRecordKind::Frame;
    std::uint64_t accepted = 0U;
    for (std::uint64_t index = 0U; index < 50'000U; ++index) {
        record.lost_before = index;
        if (queue->try_push(record)) {
            ++accepted;
        }
    }
    NX_CHECK(accepted == 50'000U);
    NX_CHECK(queue->size() == 50'000U);
    std::uint64_t rejected = 0U;
    for (std::uint64_t index = 0U; index < 30'000U; ++index) {
        record.lost_before = 100'000U + index;
        if (!queue->try_push(record)) {
            ++rejected;
        }
    }
    NX_CHECK(rejected > 0U);
    NX_CHECK(queue->is_full());

    canview::CanIngressRecord popped{};
    std::uint64_t drained = 0U;
    while (queue->try_pop(popped)) {
        ++drained;
    }
    NX_CHECK(drained == canview::IngressQueue::capacity());
}

} // namespace

namespace canview_test {

void run_resources_tests() {
    test_resource_guard();
    test_allocation_failures();
    test_burst_queue();
}

} // namespace canview_test
