// Test-only time-base publisher for the nexigen-traced smoke test.
//
// The daemon requires a published base; this fixture creates
// `/nexigen-timebase`, publishes a Ready block covering a wide validity window,
// and waits for SIGTERM/SIGINT. It is not installed and is used only by
// smoke_test.sh.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <pthread.h>
#include <signal.h>

#include <nexigen/chrono/detail/time_base_segment.hpp>
#include <nexigen/chrono/detail/time_base_seqlock.hpp>
#include <nexigen/chrono/domain_coefficient.hpp>
#include <nexigen/chrono/lock_state.hpp>
#include <nexigen/chrono/time_base_block.hpp>
#include <nexigen/chrono/time_base_host_domain.hpp>
#include <nexigen/chrono/time_base_status.hpp>
#include <nexigen/chrono/timestamp.hpp>

namespace chrono = nexigen::chrono;

int main() {
    auto segment = chrono::detail::TimeBaseSegment::create("/nexigen-timebase", 0x7465U);
    if (!segment) {
        std::fprintf(stderr, "timebase fixture: cannot create /nexigen-timebase\n");
        return 1;
    }

    const auto now = chrono::Timestamp::now();
    if (!now) {
        return 1;
    }
    const std::uint64_t mono = now->nanoseconds();

    chrono::TimeBaseBlock block{};
    block.status = chrono::TimeBaseStatus::Ready;
    block.boot_token = 0x7465U;
    block.domain_count = 2U;
    block.rows[0].domain_id = chrono::k_monotonic_raw_domain_id;
    block.rows[0].domain_class = chrono::TimeDomainClass::SystemClock;
    block.rows[0].lock = chrono::LockState::Locked;
    block.rows[0].scale_fwd_q32 = 1ULL << 32U;
    block.rows[0].scale_rev_q32 = 1ULL << 32U;
    block.rows[0].native_anchor = mono;
    block.rows[0].canonical_anchor_ns = static_cast<std::int64_t>(mono);
    block.rows[0].valid_from_canonical = 0;
    block.rows[0].valid_to_canonical = mono + 604'800'000'000'000ULL;
    block.rows[0].valid_from_native = 0;
    block.rows[0].valid_to_native = mono + 604'800'000'000'000ULL;
    block.rows[1].domain_id = chrono::k_realtime_domain_id;
    block.rows[1].domain_class = chrono::TimeDomainClass::SystemClock;
    block.rows[1].lock = chrono::LockState::Locked;
    block.rows[1].epoch = 1U;
    block.rows[1].scale_fwd_q32 = 1ULL << 32U;
    block.rows[1].scale_rev_q32 = 1ULL << 32U;
    block.rows[1].native_anchor = mono + 1'000'000U; // one millisecond offset
    block.rows[1].canonical_anchor_ns = static_cast<std::int64_t>(mono);
    block.rows[1].valid_from_canonical = 0;
    block.rows[1].valid_to_canonical = mono + 604'800'000'000'000ULL;
    block.rows[1].valid_from_native = 0;
    block.rows[1].valid_to_native = mono + 604'800'000'000'000ULL;

    chrono::detail::TimeBaseWriter{*segment->block()}.publish(block, mono + 604'800'000'000'000ULL);

    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGINT);
    if (::pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) {
        return 1;
    }
    siginfo_t info{};
    static_cast<void>(::sigwaitinfo(&signals, &info));
    segment->unlink();
    return 0;
}
