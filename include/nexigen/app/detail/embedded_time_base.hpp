// nexigen::app::detail — in-process published time base for examples and tests
// (change add-app-runtime-facade; spec app/runtime "Time base is external by
// default and embeddable explicitly").
//
// Production applications attach to the daemon's published segment. The
// App<Policy> Embedded mode needs a self-contained publisher for examples and
// smoke tests, so this helper creates the same shared-memory layout and
// publishes one Ready block before Infrastructure initialization. It is
// deliberately in detail/: it is an example/test facility, not a production
// time source.
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

namespace nexigen::app::detail {

/// @brief Self-contained published time base for one process.
///
/// The segment name includes the pid and the caller suffix, so concurrent
/// examples do not attach to each other's blocks.
///
/// @note Spec `app/runtime`, requirement "Time base is external by default and
///       embeddable explicitly".
/// @note This is an example/test facility, not a production time source.
class EmbeddedTimeBase {
public:
    /// @brief Create the segment and publish one Ready host-only block.
    ///
    /// @param suffix Distinguishes this process's segment from a concurrent one.
    explicit EmbeddedTimeBase(std::string_view suffix) {
        name_ = "/nexigen-example-timebase-" + std::to_string(static_cast<long>(::getpid())) + "-" +
                std::string(suffix);
        auto created = chrono::detail::TimeBaseSegment::create(name_, 0x4558U);
        if (!created) {
            return;
        }
        segment_.emplace(std::move(*created));
        publish();
        static_cast<void>(chrono::TimeBaseClock::attach(name_));
    }

    /// @brief The POSIX shared-memory name an infrastructure must attach to.
    /// @return The pid-qualified segment name; valid even when open failed.
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    /// @brief Whether the segment was created successfully.
    /// @return `true` when the shared-memory segment exists and was published.
    [[nodiscard]] bool is_open() const noexcept { return segment_.has_value(); }

private:
    /// @brief Publish one Ready host-only block with monotonic and realtime rows.
    ///
    /// The block is valid for one hour so an example never reports staleness.
    void publish() {
        const auto now = chrono::Timestamp::now();
        if (!now || !segment_) {
            return;
        }
        const std::uint64_t mono = now->nanoseconds();
        std::int64_t utc_offset = 0;
        static_cast<void>(chrono::sample_utc_offset(utc_offset));
        chrono::TimeBaseBlock block{};
        block.status = chrono::TimeBaseStatus::Ready;
        block.boot_token = 0x4558U;
        block.domain_count = 2U;
        block.utc_offset_ns = utc_offset;

        auto set_name = [](chrono::DomainCoefficient& row, std::string_view name) {
            const std::size_t take =
                name.size() < sizeof(row.name) - 1U ? name.size() : sizeof(row.name) - 1U;
            for (std::size_t i = 0; i < take; ++i) {
                row.name[i] = name[i];
            }
            row.name[take] = '\0';
        };

        block.rows[0].domain_id = chrono::k_monotonic_raw_domain_id;
        block.rows[0].domain_class = chrono::TimeDomainClass::SystemClock;
        block.rows[0].lock = chrono::LockState::Locked;
        block.rows[0].scale_fwd_q32 = 1ULL << 32U;
        block.rows[0].scale_rev_q32 = 1ULL << 32U;
        block.rows[0].flags = chrono::DomainCoefficient::has_reverse_bit;
        block.rows[0].native_anchor = mono;
        block.rows[0].canonical_anchor_ns = static_cast<std::int64_t>(mono);
        block.rows[0].valid_from_native = mono;
        block.rows[0].valid_to_native = mono + 60'000'000'000ULL;
        block.rows[0].valid_from_canonical = mono;
        block.rows[0].valid_to_canonical = mono + 60'000'000'000ULL;
        set_name(block.rows[0], "monotonic_raw");

        block.rows[1].domain_id = chrono::k_realtime_domain_id;
        block.rows[1].domain_class = chrono::TimeDomainClass::SystemClock;
        block.rows[1].lock = chrono::LockState::Locked;
        block.rows[1].epoch = 1U;
        block.rows[1].scale_fwd_q32 = 1ULL << 32U;
        block.rows[1].scale_rev_q32 = 1ULL << 32U;
        block.rows[1].flags = chrono::DomainCoefficient::has_reverse_bit;
        block.rows[1].native_anchor = mono + static_cast<std::uint64_t>(utc_offset);
        block.rows[1].canonical_anchor_ns = static_cast<std::int64_t>(mono);
        block.rows[1].valid_from_native = mono + static_cast<std::uint64_t>(utc_offset);
        block.rows[1].valid_to_native =
            mono + static_cast<std::uint64_t>(utc_offset) + 60'000'000'000ULL;
        block.rows[1].valid_from_canonical = mono;
        block.rows[1].valid_to_canonical = mono + 60'000'000'000ULL;
        set_name(block.rows[1], "realtime");
        chrono::detail::TimeBaseWriter writer{*segment_->block()};
        writer.publish(block, mono + 3'600'000'000'000ULL); // example: never stale
    }

    std::string name_{};
    std::optional<chrono::detail::TimeBaseSegment> segment_{};
};

} // namespace nexigen::app::detail
