// nexigen::tests — published time base for module test binaries.
//
// The production sinks derive UTC from the daemon's published time base and
// never sample the wall clock themselves. Module test binaries therefore
// publish a real shared-memory block once at startup: the canonical row is the
// identity map, and the UTC offset is the sampled `CLOCK_REALTIME -
// CLOCK_MONOTONIC_RAW` value, so timestamps match the pre-change expectations
// while the sink code exercises only the published-base path.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <unistd.h>

#include <nexigen/chrono/detail/time_base_segment.hpp>
#include <nexigen/chrono/detail/time_base_seqlock.hpp>
#include <nexigen/chrono/time_base_clock.hpp>
#include <nexigen/chrono/time_base_host_domain.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/chrono/utc_offset.hpp>

namespace nexigen::tests {

/// @brief Publish a Ready host-only block and bind `TimeBaseClock` to it.
///
/// Idempotent per process: the first call creates the segment, later calls are
/// no-ops. The segment lives until process exit.
///
/// @param suffix Distinguishes the segment from other binaries of the run.
inline void install_test_time_base(std::string_view suffix = "module") {
    static std::optional<nexigen::chrono::detail::TimeBaseSegment> segment;
    static std::string name;
    struct DetachAtExit {
        ~DetachAtExit() { nexigen::chrono::TimeBaseClock::detach(); }
    };
    static DetachAtExit detach_guard;
    (void)detach_guard;
    if (segment.has_value()) {
        if (!nexigen::chrono::TimeBaseClock::attached()) {
            static_cast<void>(nexigen::chrono::TimeBaseClock::attach(name));
        }
        return;
    }
    name = "/nexigen-test-tb-" + std::to_string(static_cast<long>(::getpid())) + "-" +
           std::string(suffix);
    auto created = nexigen::chrono::detail::TimeBaseSegment::create(name, 0x7465U);
    if (!created) {
        return;
    }
    segment.emplace(std::move(*created));

    const auto now = nexigen::chrono::Timestamp::now();
    if (!now) {
        return;
    }
    const std::uint64_t mono = now->nanoseconds();
    std::int64_t utc_offset = 0;
    static_cast<void>(nexigen::chrono::sample_utc_offset(utc_offset));

    nexigen::chrono::TimeBaseBlock block{};
    block.status = nexigen::chrono::TimeBaseStatus::Ready;
    block.boot_token = 0x7465U;
    block.domain_count = 2U;
    block.utc_offset_ns = utc_offset;
    block.rows[0].domain_id = nexigen::chrono::k_monotonic_raw_domain_id;
    block.rows[0].domain_class = nexigen::chrono::TimeDomainClass::SystemClock;
    block.rows[0].lock = nexigen::chrono::LockState::Locked;
    block.rows[0].scale_fwd_q32 = 1ULL << 32U;
    block.rows[0].native_anchor = mono;
    block.rows[0].canonical_anchor_ns = static_cast<std::int64_t>(mono);
    block.rows[0].valid_from_native =
        mono > 86'400'000'000'000ULL ? mono - 86'400'000'000'000ULL : 0U;
    block.rows[0].valid_to_native = mono + 604'800'000'000'000ULL;
    block.rows[0].valid_from_canonical =
        mono > 86'400'000'000'000ULL ? mono - 86'400'000'000'000ULL : 0U;
    block.rows[0].valid_to_canonical = mono + 604'800'000'000'000ULL;
    block.rows[0].scale_rev_q32 = 1ULL << 32U;
    block.rows[0].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;
    block.rows[1].domain_id = nexigen::chrono::k_realtime_domain_id;
    block.rows[1].domain_class = nexigen::chrono::TimeDomainClass::SystemClock;
    block.rows[1].lock = nexigen::chrono::LockState::Locked;
    block.rows[1].epoch = 1U;
    block.rows[1].native_anchor =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(mono) + utc_offset);
    block.rows[1].canonical_anchor_ns = static_cast<std::int64_t>(mono);
    block.rows[1].scale_fwd_q32 = 1ULL << 32U;
    block.rows[1].scale_rev_q32 = 1ULL << 32U;
    block.rows[1].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;
    block.rows[1].valid_from_native = block.rows[1].native_anchor;
    block.rows[1].valid_to_native = block.rows[1].native_anchor + 604'800'000'000'000ULL;
    block.rows[1].valid_from_canonical =
        mono > 86'400'000'000'000ULL ? mono - 86'400'000'000'000ULL : 0U;
    block.rows[1].valid_to_canonical = mono + 604'800'000'000'000ULL;
    nexigen::chrono::detail::TimeBaseWriter{*segment->block()}.publish(
        block, mono + 604'800'000'000'000ULL);
    static_cast<void>(nexigen::chrono::TimeBaseClock::attach(name));
}

/// @brief Publish a custom Ready block and bind it to `TimeBaseClock`.
///
/// Destroys the local owner after binding: the clock keeps its own mapping, so
/// the block stays readable even though the shared-memory name is unlinked.
///
/// @param suffix Distinguishes the segment.
/// @param utc_offset Signed canonical-to-UTC offset published in the block.
/// @param calibrated When `false`, no Realtime row is published.
/// @return The segment name that was bound.
inline std::string publish_test_time_base(std::string_view suffix, std::int64_t utc_offset,
                                          bool calibrated) {
    const std::string name = "/nexigen-test-tb-custom-" +
                             std::to_string(static_cast<long>(::getpid())) + "-" +
                             std::string(suffix);
    auto created = nexigen::chrono::detail::TimeBaseSegment::create(name, 0x7465U);
    if (!created) {
        return {};
    }
    nexigen::chrono::detail::TimeBaseSegment segment = std::move(*created);
    const auto now = nexigen::chrono::Timestamp::now();
    if (!now) {
        return {};
    }
    const std::uint64_t mono = now->nanoseconds();
    nexigen::chrono::TimeBaseBlock block{};
    block.status = nexigen::chrono::TimeBaseStatus::Ready;
    block.boot_token = 0x7465U;
    block.domain_count = calibrated ? 2U : 1U;
    block.utc_offset_ns = utc_offset;
    block.rows[0].domain_id = nexigen::chrono::k_monotonic_raw_domain_id;
    block.rows[0].domain_class = nexigen::chrono::TimeDomainClass::SystemClock;
    block.rows[0].lock = nexigen::chrono::LockState::Locked;
    block.rows[0].scale_fwd_q32 = 1ULL << 32U;
    block.rows[0].native_anchor = mono;
    block.rows[0].canonical_anchor_ns = static_cast<std::int64_t>(mono);
    block.rows[0].valid_from_native =
        mono > 86'400'000'000'000ULL ? mono - 86'400'000'000'000ULL : 0U;
    block.rows[0].valid_to_native = mono + 604'800'000'000'000ULL;
    block.rows[0].valid_from_canonical =
        mono > 86'400'000'000'000ULL ? mono - 86'400'000'000'000ULL : 0U;
    block.rows[0].valid_to_canonical = mono + 604'800'000'000'000ULL;
    block.rows[0].scale_rev_q32 = 1ULL << 32U;
    block.rows[0].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;
    if (calibrated) {
        block.rows[1].domain_id = nexigen::chrono::k_realtime_domain_id;
        block.rows[1].domain_class = nexigen::chrono::TimeDomainClass::SystemClock;
        block.rows[1].lock = nexigen::chrono::LockState::Locked;
        block.rows[1].epoch = 1U;
        block.rows[1].native_anchor =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(mono) + utc_offset);
        block.rows[1].canonical_anchor_ns = static_cast<std::int64_t>(mono);
        block.rows[1].scale_fwd_q32 = 1ULL << 32U;
        block.rows[1].scale_rev_q32 = 1ULL << 32U;
        block.rows[1].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;
        block.rows[1].valid_from_native = block.rows[1].native_anchor;
        block.rows[1].valid_to_native = block.rows[1].native_anchor + 604'800'000'000'000ULL;
        block.rows[1].valid_from_canonical =
            mono > 86'400'000'000'000ULL ? mono - 86'400'000'000'000ULL : 0U;
        block.rows[1].valid_to_canonical = mono + 604'800'000'000'000ULL;
    }
    nexigen::chrono::detail::TimeBaseWriter{*segment.block()}.publish(
        block, mono + 604'800'000'000'000ULL);
    if (!nexigen::chrono::TimeBaseClock::attach(name).has_value()) {
        return {};
    }
    return name;
}

/// @brief Release the process-wide binding before exit (LeakSanitizer-friendly).
inline void detach_test_time_base() noexcept {
    nexigen::chrono::TimeBaseClock::detach();
}

} // namespace nexigen::tests
