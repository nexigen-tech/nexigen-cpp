#include "heatmap.hpp"

#include <algorithm>
#include <format>

namespace nexigen::canview {

namespace {

constexpr std::uint64_t max_step_ns{3'600'000'000'000ULL};

std::uint64_t id_bin(std::uint32_t can_id) noexcept {
    const std::uint64_t bin = can_id / 228U;
    return bin < HeatmapWindow::id_bins ? bin : HeatmapWindow::id_bins - 1U;
}

double ratio(std::uint64_t part, std::uint64_t whole) noexcept {
    if (whole == 0U) {
        return 0.0;
    }
    const double value = static_cast<double>(part) / static_cast<double>(whole);
    return std::min(value, 1.0);
}

} // namespace

void HeatmapWindow::init(std::size_t max_columns_value) noexcept {
    columns_.clear();
    column_cap_ = std::max<std::size_t>(1U, std::min(max_columns_value, max_columns));
    requested_step_ns_ = 10'000'000ULL;
    effective_step_ns_ = requested_step_ns_;
    axis_ = HeatmapAxis::SharedHardware;
    selected_channel_ = 0U;
    follow_ = true;
}

void HeatmapWindow::step_coarser() noexcept {
    requested_step_ns_ = std::min(requested_step_ns_ * 2U, max_step_ns);
}

void HeatmapWindow::step_finer() noexcept {
    requested_step_ns_ = std::max(requested_step_ns_ / 2U, min_step_ns);
}

void HeatmapWindow::ensure_column(std::size_t index) {
    while (columns_.size() <= index && columns_.size() < column_cap_) {
        columns_.push_back(HeatmapColumn{});
    }
}

std::optional<std::size_t> HeatmapWindow::column_for(
    const CanRecordView& record, std::size_t channel, HeatmapAxis axis, const TimeContext* context,
    std::uint64_t range_start, std::uint64_t range_end, std::uint64_t step,
    std::array<std::uint64_t, channels>& channel_base, std::array<bool, channels>& channel_started,
    std::optional<std::uint64_t>& utc_base) const noexcept {
    const nexigen::hw::can::CanMessage& message = *record.message;
    std::uint64_t value = message.hardware_timestamp_ns;
    if (axis == HeatmapAxis::Utc) {
        if (context == nullptr) {
            return std::nullopt;
        }
        const auto utc = TimePresenter::utc_ns(record, *context);
        if (!utc.has_value()) {
            return std::nullopt;
        }
        value = static_cast<std::uint64_t>(*utc);
        if (!utc_base.has_value()) {
            utc_base = value;
        }
    }
    std::uint64_t base = range_start;
    if (axis == HeatmapAxis::Utc) {
        base = *utc_base;
    } else if (axis == HeatmapAxis::HardwareLanes) {
        if (!channel_started[channel]) {
            channel_base[channel] = value;
            channel_started[channel] = true;
        }
        base = channel_base[channel];
    } else if (value < range_start || value >= range_end) {
        return std::nullopt;
    }
    if (value < base) {
        return std::nullopt;
    }
    const std::uint64_t column = (value - base) / step;
    if (column >= max_columns) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(column);
}

void HeatmapWindow::accumulate(const CanRecordView& record, HeatmapColumn& bucket,
                               const std::array<ChannelTiming, channels>& timings,
                               std::uint16_t selected_channel) const noexcept {
    const nexigen::hw::can::CanMessage& message = *record.message;
    const std::size_t channel = message.channel_id % channels;
    HeatmapCell& cell = bucket.channels[channel];
    if (record.kind() == CanRecordKind::BusIdleHeartbeat) {
        if (message.idle_duration_ns > 0U) {
            cell.idle_ns += message.idle_duration_ns;
            cell.measured = true;
        }
        return;
    }
    if (!record.is_frame()) {
        return;
    }
    if (message.kind == nexigen::hw::can::CanMessageKind::Error) {
        ++cell.errors;
    } else if (message.kind == nexigen::hw::can::CanMessageKind::Overload) {
        ++cell.overloads;
    }
    cell.frames = true;
    const FrameTiming timing = estimate_frame_timing(message, timings[channel]);
    if (!timing.available) {
        cell.missing_bitrate = true;
        return;
    }
    cell.bit_time_ns += timing.time_ns;
    if (channel == (selected_channel % channels)) {
        HeatmapIdBin& bin = bucket.bins[id_bin(message.can_id)];
        bin.bit_time_ns += timing.time_ns;
        ++bin.frames;
    }
}

void HeatmapWindow::rebuild(const RecordSource& source, const TimeContext* context,
                            HeatmapAxis axis, std::uint64_t range_start, std::uint64_t range_end,
                            std::uint16_t selected_channel,
                            const std::array<ChannelTiming, channels>& timings) {
    columns_.clear();
    if (fail_allocation_) {
        return;
    }
    axis_ = axis;
    selected_channel_ = selected_channel;
    if (range_end <= range_start) {
        effective_step_ns_ = requested_step_ns_;
        return;
    }
    const std::uint64_t span = range_end - range_start;
    const std::uint64_t needed = (span + column_cap_ - 1U) / column_cap_;
    effective_step_ns_ = std::max({requested_step_ns_, needed, min_step_ns});
    const std::uint64_t step = effective_step_ns_;

    std::array<std::uint64_t, channels> channel_base{};
    std::array<bool, channels> channel_started{};
    std::optional<std::uint64_t> utc_base{};

    for (std::uint64_t seq = source.head(); seq < source.tail(); ++seq) {
        const auto record = source.find(seq);
        if (!record.has_value() || record->message == nullptr) {
            continue;
        }
        const std::size_t channel = record->message->channel_id % channels;
        const auto column = column_for(*record, channel, axis, context, range_start, range_end,
                                       step, channel_base, channel_started, utc_base);
        if (!column.has_value()) {
            continue;
        }
        ensure_column(*column);
        if (columns_.size() <= *column) {
            break;
        }
        HeatmapColumn& bucket = columns_[*column];
        if (bucket.first_seq == 0U) {
            bucket.first_seq = record->seq;
        }
        accumulate(*record, bucket, timings, selected_channel);
    }
}

const HeatmapColumn* HeatmapWindow::column(std::size_t index) const noexcept {
    if (index >= columns_.size()) {
        return nullptr;
    }
    return &columns_[index];
}

std::optional<std::uint64_t> HeatmapWindow::click_record(std::size_t column) const noexcept {
    if (column >= columns_.size() || columns_[column].first_seq == 0U) {
        return std::nullopt;
    }
    return columns_[column].first_seq;
}

HeatmapLoad HeatmapWindow::load(std::size_t column, std::uint16_t channel_id) const noexcept {
    HeatmapLoad out{};
    if (column >= columns_.size()) {
        return out;
    }
    const HeatmapCell& cell = columns_[column].channels[channel_id % channels];
    out.measured_available = cell.measured;
    out.estimated_available = cell.frames && !cell.missing_bitrate;
    out.missing_bitrate = cell.missing_bitrate;
    out.measured = cell.measured ? 1.0 - ratio(cell.idle_ns, effective_step_ns_) : 0.0;
    out.estimated = cell.frames ? ratio(cell.bit_time_ns, effective_step_ns_) : 0.0;
    return out;
}

std::string HeatmapWindow::to_string() const {
    return std::format("HeatmapWindow(columns={}, step_ns={}, requested_ns={}, axis={}, "
                       "selected={}, follow={})",
                       columns_.size(), effective_step_ns_, requested_step_ns_, axis_,
                       selected_channel_, follow_);
}

} // namespace nexigen::canview
