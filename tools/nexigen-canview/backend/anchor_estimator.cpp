#include "anchor_estimator.hpp"

#include <algorithm>
#include <cstdlib>

#include <nexigen/chrono/time_base_clock.hpp>

namespace nexigen::canview {

std::optional<std::int64_t> default_utc_lookup(std::int64_t canonical_ns,
                                               void* /*context*/) noexcept {
    return nexigen::chrono::TimeBaseClock::utc_from_canonical(canonical_ns);
}

void AnchorEstimator::clear_window() noexcept {
    window_head_ = 0;
    window_count_ = 0;
}

void AnchorEstimator::add_sample(Sample sample) noexcept {
    if (window_count_ < window_size) {
        window_[(window_head_ + window_count_) % window_size] = sample;
        ++window_count_;
        return;
    }
    window_[window_head_] = sample;
    window_head_ = (window_head_ + 1U) % window_size;
}

std::int64_t AnchorEstimator::minimum_offset() const noexcept {
    if (window_count_ == 0U) {
        return 0;
    }
    std::int64_t minimum = window_[window_head_].offset_ns;
    for (std::size_t index = 1U; index < window_count_; ++index) {
        const Sample& sample = window_[(window_head_ + index) % window_size];
        minimum = std::min(minimum, sample.offset_ns);
    }
    return minimum;
}

std::int32_t AnchorEstimator::compute_drift() const noexcept {
    if (window_count_ < 2U) {
        return 0;
    }
    const Sample& oldest = window_[window_head_];
    const Sample& newest = window_[(window_head_ + window_count_ - 1U) % window_size];
    if (newest.hardware_ns <= oldest.hardware_ns) {
        return 0;
    }
    const std::int64_t delta_offset = newest.offset_ns - oldest.offset_ns;
    const auto delta_time = static_cast<std::int64_t>(newest.hardware_ns - oldest.hardware_ns);
    const long double ppb = (static_cast<long double>(delta_offset) * 1'000'000'000.0L) /
                            static_cast<long double>(delta_time);
    if (ppb >= 2'000'000'000.0L) {
        return 2'000'000'000;
    }
    if (ppb <= -2'000'000'000.0L) {
        return -2'000'000'000;
    }
    return static_cast<std::int32_t>(ppb);
}

std::optional<AnchorEvent>
AnchorEstimator::observe(const nexigen::com::EventHeader& header,
                         const nexigen::hw::can::CanMessage& message) noexcept {
    if (lookup_ == nullptr) {
        return std::nullopt;
    }
    const auto utc_ns = lookup_(static_cast<std::int64_t>(header.ingest_timestamp_ns), context_);
    if (!utc_ns.has_value()) {
        return std::nullopt;
    }

    const auto source = static_cast<std::uint8_t>(message.timestamp_source);
    const auto hardware_ns = message.hardware_timestamp_ns;
    const std::int64_t offset = *utc_ns - static_cast<std::int64_t>(hardware_ns);

    bool new_epoch = !has_anchor_;
    if (has_anchor_) {
        if (source_known_ && source != source_) {
            new_epoch = true;
        } else if (std::llabs(offset - published_offset_ns_) > material_jump_ns) {
            new_epoch = true;
        }
    }
    if (new_epoch) {
        if (has_anchor_) {
            ++epoch_;
        }
        source_ = source;
        source_known_ = true;
        has_anchor_ = true;
        clear_window();
        first_hw_ns_ = hardware_ns;
        first_offset_ns_ = offset;
        add_sample(Sample{offset, hardware_ns});
        published_offset_ns_ = offset;
        drift_ppb_ = 0;
        last_publish_hw_ns_ = hardware_ns;
        AnchorEvent event{};
        event.channel_id = channel_id_;
        event.epoch = epoch_;
        event.source = source_;
        event.utc_offset_ns = published_offset_ns_;
        event.drift_ppb = drift_ppb_;
        event.valid_from_hw_ns = hardware_ns;
        return event;
    }

    source_ = source;
    source_known_ = true;
    add_sample(Sample{offset, hardware_ns});
    const std::int64_t minimum = minimum_offset();
    const std::int32_t drift = compute_drift();
    const bool resolved = std::llabs(minimum - published_offset_ns_) > publish_resolution_ns;
    const bool periodic = hardware_ns > last_publish_hw_ns_ &&
                          (hardware_ns - last_publish_hw_ns_) >= publish_period_ns;
    if (!resolved && !periodic) {
        return std::nullopt;
    }
    published_offset_ns_ = minimum;
    drift_ppb_ = drift;
    last_publish_hw_ns_ = hardware_ns;
    AnchorEvent event{};
    event.channel_id = channel_id_;
    event.epoch = epoch_;
    event.source = source_;
    event.utc_offset_ns = published_offset_ns_;
    event.drift_ppb = drift_ppb_;
    event.valid_from_hw_ns = hardware_ns;
    return event;
}

} // namespace nexigen::canview
