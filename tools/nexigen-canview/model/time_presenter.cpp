#include "time_presenter.hpp"

#include <charconv>
#include <cstdlib>

#include <nexigen/chrono/civil_time.hpp>

namespace nexigen::canview {

namespace {

void set_reason(TimeCell& cell, std::string_view reason) noexcept {
    std::size_t index = 0U;
    while (index + 1U < cell.reason.size() && index < reason.size()) {
        cell.reason[index] = reason[index];
        ++index;
    }
    cell.reason[index] = '\0';
}

void format_signed(std::int64_t value, TimeCell& cell) noexcept {
    cell.value = value;
    char* const begin = cell.text.data();
    char* const end = begin + cell.text.size() - 1U;
    const auto result = std::to_chars(begin, end, value);
    if (result.ec == std::errc{}) {
        *result.ptr = '\0';
    } else {
        cell.text[0] = '\0';
        cell.available = false;
    }
    cell.negative = value < 0;
}

bool has_hardware_time(const CanRecordView& record) noexcept {
    return record.message != nullptr &&
           (record.message->frame_flags.has(
                nexigen::hw::can::CanFrameFlags::has_hardware_timestamp) ||
            record.message->hardware_timestamp_ns != 0U);
}

TimeCell unavailable_cell(std::string_view reason = {}) noexcept {
    TimeCell cell{};
    cell.text[0] = '-';
    cell.text[1] = '\0';
    set_reason(cell, reason);
    return cell;
}

std::int64_t drift_adjusted_offset(const AnchorEvent& anchor, std::uint64_t hardware_ns) noexcept {
    if (anchor.drift_ppb == 0 || hardware_ns <= anchor.valid_from_hw_ns) {
        return anchor.utc_offset_ns;
    }
    const auto elapsed = static_cast<std::int64_t>(hardware_ns - anchor.valid_from_hw_ns);
    const long double drift = static_cast<long double>(anchor.drift_ppb);
    const long double correction = (drift * static_cast<long double>(elapsed)) / 1'000'000'000.0L;
    return anchor.utc_offset_ns + static_cast<std::int64_t>(correction);
}

} // namespace

void TimeContext::observe(const CanRecordView& record) noexcept {
    if (record.message == nullptr || !has_hardware_time(record)) {
        return;
    }
    const std::size_t channel = record.message->channel_id % channels;
    if (!session_started[channel]) {
        session_start[channel] = record.message->hardware_timestamp_ns;
        session_started[channel] = true;
    }
}

void TimeContext::reset_session() noexcept {
    session_start.fill(0U);
    session_started.fill(false);
}

std::optional<AnchorEvent> TimePresenter::anchor(const CanRecordView& record,
                                                 const TimeContext& context) noexcept {
    if (record.message == nullptr) {
        return std::nullopt;
    }
    return context.anchors.lookup(record.message->channel_id, record.time_epoch);
}

std::optional<std::int64_t> TimePresenter::utc_ns(const CanRecordView& record,
                                                  const TimeContext& context) noexcept {
    if (!has_hardware_time(record)) {
        return std::nullopt;
    }
    const auto anchor_event = anchor(record, context);
    if (!anchor_event.has_value()) {
        return std::nullopt;
    }
    const std::uint64_t hardware = record.message->hardware_timestamp_ns;
    const std::int64_t offset = drift_adjusted_offset(*anchor_event, hardware);
    return static_cast<std::int64_t>(hardware) + offset;
}

TimeCell TimePresenter::utc(const CanRecordView& record, const TimeContext& context) noexcept {
    const auto value = utc_ns(record, context);
    if (!value.has_value()) {
        return unavailable_cell("no UTC anchor for this epoch");
    }
    TimeCell cell{};
    char iso[31]{};
    nexigen::chrono::format_utc_iso8601_ns(*value, iso);
    std::size_t index = 0U;
    while (index + 1U < cell.text.size() && iso[index] != '\0') {
        cell.text[index] = iso[index];
        ++index;
    }
    cell.text[index] = '\0';
    cell.available = index > 0U;
    cell.estimated = true;
    return cell;
}

TimeCell TimePresenter::hardware(const CanRecordView& record, TimestampMode mode,
                                 const CanView& view, const TimeContext& context) noexcept {
    if (!has_hardware_time(record)) {
        return unavailable_cell("no usable hardware timestamp");
    }
    const auto hardware_ns = static_cast<std::int64_t>(record.message->hardware_timestamp_ns);
    const std::uint16_t channel = record.message->channel_id;
    std::int64_t value = hardware_ns;
    switch (mode) {
        case TimestampMode::Driver: break;
        case TimestampMode::SinceSessionStart:
            if (!context.has_session_start(channel)) {
                return unavailable_cell("session start is unavailable");
            }
            value = hardware_ns - static_cast<std::int64_t>(context.session_start_ns(channel));
            break;
        case TimestampMode::RelativeT0:
            if (!view.has_t0()) {
                return unavailable_cell("T0 is not set");
            }
            if (!context.assume_shared_timebase && view.t0_channel() != channel) {
                return unavailable_cell("T0 belongs to another channel");
            }
            value = hardware_ns - view.t0_ns();
            break;
    }
    TimeCell cell{};
    format_signed(value, cell);
    cell.available = true;
    return cell;
}

TimeCell TimePresenter::delta(const CanRecordView& record, const CanRecordView* previous,
                              DeltaMode mode, const CanView& view,
                              const TimeContext& context) noexcept {
    if (!has_hardware_time(record)) {
        return unavailable_cell("no usable hardware timestamp");
    }
    const auto current = static_cast<std::int64_t>(record.message->hardware_timestamp_ns);
    const std::uint16_t channel = record.message->channel_id;
    std::int64_t value = 0;
    switch (mode) {
        case DeltaMode::PerCanId:
            if (!record.is_frame()) {
                return unavailable_cell("not a received frame");
            }
            value = record.delta_same_id_ns;
            break;
        case DeltaMode::PreviousVisible:
            if (previous == nullptr || previous->message == nullptr ||
                !has_hardware_time(*previous)) {
                return unavailable_cell("no previous visible record");
            }
            if (!context.assume_shared_timebase && previous->message->channel_id != channel) {
                return unavailable_cell("previous record is on another channel");
            }
            value = current - static_cast<std::int64_t>(previous->message->hardware_timestamp_ns);
            break;
        case DeltaMode::FromT0:
            if (!view.has_t0()) {
                return unavailable_cell("T0 is not set");
            }
            if (!context.assume_shared_timebase && view.t0_channel() != channel) {
                return unavailable_cell("T0 belongs to another channel");
            }
            value = current - view.t0_ns();
            break;
    }
    TimeCell cell{};
    format_signed(value, cell);
    cell.available = true;
    return cell;
}

bool TimePresenter::is_anomalous(std::int64_t delta_ns, std::int64_t threshold_ns) noexcept {
    if (threshold_ns <= 0) {
        return false;
    }
    const std::int64_t magnitude = delta_ns < 0 ? -delta_ns : delta_ns;
    return magnitude > threshold_ns;
}

} // namespace nexigen::canview
