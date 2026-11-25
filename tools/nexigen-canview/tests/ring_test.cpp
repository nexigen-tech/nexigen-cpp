// Ring layout, eviction and SPSC queue tests.
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include <nexigen/hw/can/can_message.hpp>

#include "budget.hpp"
#include "can_ingress_record.hpp"
#include "can_queues.hpp"
#include "can_record_ring.hpp"
#include "record_meta.hpp"
#include "suite.hpp"
#include "support/check.hpp"

namespace {

namespace canview = nexigen::canview;
namespace hwcan = nexigen::hw::can;

void* failing_map(std::size_t) noexcept {
    return nullptr;
}

hwcan::CanMessage make_frame(std::uint64_t index) {
    hwcan::CanMessage message{};
    message.hardware_timestamp_ns = 1'000U + index;
    message.hardware_sequence = index;
    message.channel_id = 1U;
    message.can_id = static_cast<std::uint32_t>(0x100U + (index % 8U));
    message.dlc = 8U;
    message.data[0] = static_cast<std::uint8_t>(index & 0xFFU);
    message.frame_flags.bits =
        hwcan::CanFrameFlags::has_hardware_timestamp | hwcan::CanFrameFlags::has_hardware_sequence;
    return message;
}

void test_ring_creation() {
    NX_CHECK(!canview::CanRecordRing::create(0U));
    NX_CHECK(!canview::CanRecordRing::create(3U));
    NX_CHECK(!canview::CanRecordRing::create(1024U, &failing_map));

    const auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    NX_CHECK(ring->capacity() == 1024U);
    NX_CHECK(ring->mapped_bytes() == 1024U * canview::CanRecordRing::bytes_per_slot);
    NX_CHECK(ring->size() == 0U);
    NX_CHECK(ring->head() == 0U);
    NX_CHECK(ring->tail() == 0U);
    NX_CHECK(!ring->find(0U));
}

void test_ring_push_find_evict() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());

    const std::uint8_t meta = canview::pack_frame_meta(canview::CanRecordKind::Frame);
    for (std::uint64_t index = 0U; index < 1024U; ++index) {
        const std::uint64_t seq = ring->push(make_frame(index), static_cast<std::int64_t>(index),
                                             canview::CanRecordView::no_node, meta, 3U, 7U);
        NX_CHECK(seq == index);
    }
    NX_CHECK(ring->tail() == 1024U);
    NX_CHECK(ring->size() == 1024U);
    NX_CHECK(ring->evictions() == 0U);

    const auto first = ring->find(0U);
    NX_CHECK(first.has_value());
    NX_CHECK(first->seq == 0U);
    NX_CHECK(first->message != nullptr);
    NX_CHECK(first->message->hardware_timestamp_ns == 1'000U);
    NX_CHECK(first->delta_same_id_ns == 0);
    NX_CHECK(first->co_node_index == canview::CanRecordView::no_node);
    NX_CHECK(first->time_epoch == 3U);
    NX_CHECK(first->co_map_epoch == 7U);
    NX_CHECK(first->kind() == canview::CanRecordKind::Frame);
    NX_CHECK(first->is_frame());

    const std::uint64_t next =
        ring->push(make_frame(1024U), 0, canview::CanRecordView::no_node, meta, 3U, 7U);
    NX_CHECK(next == 1024U);
    NX_CHECK(ring->head() == 1U);
    NX_CHECK(ring->size() == 1024U);
    NX_CHECK(ring->evictions() == 1U);
    NX_CHECK(!ring->find(0U));
    NX_CHECK(ring->find(1U).has_value());
    NX_CHECK(ring->find(1024U).has_value());
    NX_CHECK(ring->find(1025U) == std::nullopt);

    // Retained records keep their absolute numbers across further eviction.
    for (std::uint64_t index = 0U; index < 10U; ++index) {
        static_cast<void>(ring->push(make_frame(2000U + index), 0, canview::CanRecordView::no_node,
                                     meta, 0U, 0U));
    }
    const auto retained = ring->find(15U);
    NX_CHECK(retained.has_value());
    NX_CHECK(retained->seq == 15U);
    NX_CHECK(retained->message->hardware_sequence == 15U);

    ring->clear();
    NX_CHECK(ring->head() == 0U);
    NX_CHECK(ring->tail() == 0U);
    NX_CHECK(ring->evictions() == 0U);
    NX_CHECK(!ring->find(15U));
}

void test_synthetic_record() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());

    const std::uint8_t meta = canview::pack_gap_meta(canview::CanRecordKind::TransportGap,
                                                     canview::GapReason::HardwareSequence, true);
    hwcan::CanMessage marker{};
    marker.hardware_timestamp_ns = 123U;
    marker.channel_id = 2U;
    const std::uint64_t seq = ring->push(marker, 5, canview::CanRecordView::no_node, meta, 1U, 0U);
    const auto view = ring->find(seq);
    NX_CHECK(view.has_value());
    NX_CHECK(view->kind() == canview::CanRecordKind::TransportGap);
    NX_CHECK(!view->is_frame());
    NX_CHECK(view->gap_count() == 5U);
    NX_CHECK(canview::is_loss(view->kind()));
}

void test_budget_ring() {
    const auto ring = canview::CanRecordRing::create_for_budget(16U);
    NX_CHECK(ring.has_value());
    NX_CHECK(ring->capacity() == canview::capacity_for_budget(16U));
    NX_CHECK(ring->capacity() >= 1024U);
}

void test_queues_single_thread() {
    canview::StatusQueue statuses{};
    canview::StatusEvent status{};
    status.channel_id = 42U;
    status.status.rx_count = 9U;
    NX_CHECK(statuses.try_push(status).has_value());
    canview::StatusEvent popped{};
    NX_CHECK(statuses.try_pop(popped).has_value());
    NX_CHECK(popped.channel_id == 42U);
    NX_CHECK(popped.status.rx_count == 9U);
    NX_CHECK(!statuses.try_pop(popped));

    canview::AnchorQueue anchors{};
    canview::AnchorEvent anchor{};
    anchor.channel_id = 1U;
    anchor.epoch = 4U;
    anchor.utc_offset_ns = -1234;
    NX_CHECK(anchors.try_push(anchor).has_value());
    canview::AnchorEvent anchor_out{};
    NX_CHECK(anchors.try_pop(anchor_out).has_value());
    NX_CHECK(anchor_out.epoch == 4U);
    NX_CHECK(anchor_out.utc_offset_ns == -1234);
}

void test_queues_cross_thread() {
    auto queue = std::make_unique<canview::IngressQueue>();
    constexpr std::uint64_t count = 50'000U;
    std::atomic<bool> producer_done{false};

    std::thread producer([&] {
        for (std::uint64_t index = 0U; index < count; ++index) {
            canview::CanIngressRecord record{};
            record.lost_before = index;
            record.kind = canview::CanRecordKind::Frame;
            while (!queue->try_push(record)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::uint64_t expected = 0U;
    std::uint64_t received = 0U;
    bool ordered = true;
    canview::CanIngressRecord record{};
    while (!producer_done.load(std::memory_order_acquire) || !queue->is_empty()) {
        if (queue->try_pop(record)) {
            if (record.lost_before != expected) {
                ordered = false;
            }
            ++expected;
            ++received;
        }
    }
    producer.join();

    NX_CHECK(ordered);
    NX_CHECK(received == count);
    NX_CHECK(queue->is_empty());
    NX_CHECK(record.kind == canview::CanRecordKind::Frame);
}

} // namespace

namespace canview_test {

void run_ring_tests() {
    test_ring_creation();
    test_ring_push_find_evict();
    test_synthetic_record();
    test_budget_ring();
    test_queues_single_thread();
    test_queues_cross_thread();
}

} // namespace canview_test
