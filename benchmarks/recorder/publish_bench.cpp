// nexigen storage-format benchmark — iceoryx2 publish-path header measurement.
#include "publish_bench.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <malloc.h>
#include <pthread.h>
#include <sched.h>

#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>

namespace nexigen::bench {

namespace {

constexpr std::size_t kMeasured = 10'000;
constexpr std::size_t kWarmup = 2'000;

struct alignas(64) LogFrame256 {
    std::array<std::uint8_t, 256> bytes{};
};
static_assert(sizeof(LogFrame256) == 256);
static_assert(alignof(LogFrame256) == 64);
static_assert(nexigen::com::concepts::Message<LogFrame256>);

struct alignas(8) SampleHeader {
    std::uint64_t publisher_id{1};
    std::uint64_t sequence{0};
};
static_assert(sizeof(SampleHeader) == 16);
static_assert(nexigen::com::concepts::Message<SampleHeader>);

[[nodiscard]] std::size_t heap_in_use() noexcept {
    return static_cast<std::size_t>(mallinfo2().uordblks);
}

void pin_to_lowest_core() noexcept {
    cpu_set_t allowed{};
    if (::pthread_getaffinity_np(pthread_self(), sizeof(allowed), &allowed) != 0) {
        return;
    }
    for (std::size_t core = 0; core < static_cast<std::size_t>(CPU_SETSIZE); ++core) {
        if (CPU_ISSET(core, &allowed)) {
            cpu_set_t mine{};
            CPU_SET(core, &mine);
            static_cast<void>(
                ::pthread_setaffinity_np(pthread_self(), sizeof(mine), &mine));
            return;
        }
    }
}

} // namespace

core::Result<PublishResult> run_publish_bench() noexcept {
    using com::Node;
    using com::Publisher;
    using com::ServiceName;

    auto node = Node::create();
    if (!node.has_value()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    const auto service = ServiceName::create("Nexigen/Bench/StoragePublish");
    if (!service.has_value()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    auto publisher = Publisher<LogFrame256, SampleHeader>::create(*node, *service);
    if (!publisher.has_value()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    pin_to_lowest_core();

    const TscClock clock = TscClock::calibrate();
    const double overhead_ns = measure_tick_overhead(clock);

    LogFrame256 frame{};
    SampleHeader header{};
    std::vector<std::uint64_t> copy_ticks(kMeasured);
    std::vector<std::uint64_t> header_ticks(kMeasured);
    std::vector<std::uint64_t> push_ticks(kMeasured);
    core::SpscRingBuffer<LogFrame256, 16384> ring{};

    std::size_t failures = 0;
    for (std::size_t i = 0; i < kWarmup; ++i) {
        static_cast<void>(publisher->send_copy(frame));
        auto loaned = publisher->loan_uninit();
        if (loaned.has_value()) {
            auto sample = loaned->write_payload(frame);
            sample.user_header_mut() = header;
            static_cast<void>(publisher->send(std::move(sample)));
        }
        static_cast<void>(ring.try_push(frame));
    }

    const std::size_t heap_before = heap_in_use();

    for (std::size_t i = 0; i < kMeasured; ++i) {
        const std::uint64_t start = clock.now();
        const auto status = publisher->send_copy(frame);
        const std::uint64_t end = clock.now();
        copy_ticks[i] = end - start;
        if (!status.has_value()) {
            ++failures;
        }
    }

    for (std::size_t i = 0; i < kMeasured; ++i) {
        const std::uint64_t start = clock.now();
        auto loaned = publisher->loan_uninit();
        if (loaned.has_value()) {
            auto sample = loaned->write_payload(frame);
            sample.user_header_mut() = header;
            const auto status = publisher->send(std::move(sample));
            if (!status.has_value()) {
                ++failures;
            }
        } else {
            ++failures;
        }
        const std::uint64_t end = clock.now();
        header_ticks[i] = end - start;
    }

    for (std::size_t i = 0; i < kMeasured; ++i) {
        const std::uint64_t start = clock.now();
        const auto status = ring.try_push(frame);
        const std::uint64_t end = clock.now();
        push_ticks[i] = end - start;
        if (!status.has_value()) {
            ++failures;
        }
    }

    const std::size_t heap_after = heap_in_use();

    PublishResult result{};
    result.send_copy = compute_stats(copy_ticks, clock, overhead_ns);
    result.loan_with_header = compute_stats(header_ticks, clock, overhead_ns);
    result.producer_try_push = compute_stats(push_ticks, clock, overhead_ns);
    result.heap_delta_bytes = static_cast<std::int64_t>(heap_after) -
                              static_cast<std::int64_t>(heap_before);
    result.ok = failures == 0;
    return result;
}

} // namespace nexigen::bench
