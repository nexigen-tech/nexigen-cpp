// Model vocabulary, metadata packing, sequence, validation and budget tests.
#include <array>
#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/hw/can/can_message.hpp>

#include "budget.hpp"
#include "delta_mode.hpp"
#include "gap_reason.hpp"
#include "overwrite_key.hpp"
#include "record_kind.hpp"
#include "record_meta.hpp"
#include "sequence_tracker.hpp"
#include "suite.hpp"
#include "support/check.hpp"
#include "time_column_mode.hpp"
#include "timestamp_mode.hpp"
#include "validate_for_view.hpp"
#include "view_channel.hpp"
#include "view_kind.hpp"

namespace {

namespace canview = nexigen::canview;
namespace hwcan = nexigen::hw::can;

template <class Enum> void check_enum_vocabulary() {
    for (const auto& row : nexigen::core::EnumTraits<Enum>::entries) {
        Enum parsed{};
        NX_CHECK(canview::from_string(row.name, parsed));
        NX_CHECK(parsed == row.value);
        NX_CHECK(canview::to_string(row.value) == row.name);
        NX_CHECK(std::format("{}", row.value) == row.name);

        nlohmann::json encoded = row.value;
        NX_CHECK(encoded.is_string());
        NX_CHECK(encoded.get<std::string>() == row.name);

        Enum decoded{};
        canview::from_json(encoded, decoded);
        NX_CHECK(decoded == row.value);
    }
}

hwcan::CanMessage make_message(hwcan::CanMessageKind kind, std::uint8_t dlc,
                               std::uint32_t can_id = 0x100U, bool extended = false) {
    hwcan::CanMessage message{};
    message.kind = kind;
    message.dlc = dlc;
    message.can_id = can_id;
    message.extended = extended;
    message.channel_id = 1U;
    return message;
}

void test_enums() {
    check_enum_vocabulary<canview::ViewKind>();
    check_enum_vocabulary<canview::TimeColumnMode>();
    check_enum_vocabulary<canview::TimestampMode>();
    check_enum_vocabulary<canview::DeltaMode>();
    check_enum_vocabulary<canview::CanRecordKind>();
    check_enum_vocabulary<canview::GapReason>();
    check_enum_vocabulary<canview::SequenceOutcome>();
}

void test_channel_and_key() {
    canview::CanViewChannel channel{};
    channel.name = "powertrain";
    channel.channel_id = 7U;
    channel.nominal_bitrate = 500'000U;
    channel.data_bitrate = 2'000'000U;
    channel.fd = true;

    const nlohmann::json encoded = channel;
    canview::CanViewChannel decoded{};
    canview::from_json(encoded, decoded);
    NX_CHECK(decoded.name == channel.name);
    NX_CHECK(decoded.channel_id == channel.channel_id);
    NX_CHECK(decoded.nominal_bitrate == channel.nominal_bitrate);
    NX_CHECK(decoded.data_bitrate == channel.data_bitrate);
    NX_CHECK(decoded.fd == channel.fd);
    NX_CHECK(std::format("{}", channel) == channel.to_string());

    canview::OverwriteKey key{};
    key.channel_id = 3U;
    key.can_id = 0x18FF1234U;
    key.extended = true;
    key.kind = hwcan::CanMessageKind::FdData;

    const nlohmann::json key_json = key;
    canview::OverwriteKey key_back{};
    canview::from_json(key_json, key_back);
    NX_CHECK(key_back == key);
    NX_CHECK(std::hash<canview::OverwriteKey>{}(key) ==
             std::hash<canview::OverwriteKey>{}(key_back));
    NX_CHECK(std::format("{}", key) == key.to_string());
}

void test_record_meta() {
    for (const auto& row : nexigen::core::EnumTraits<canview::CanRecordKind>::entries) {
        const std::uint8_t meta = canview::pack_kind(row.value);
        NX_CHECK(canview::meta_kind(meta) == row.value);
        NX_CHECK(canview::meta_flags(meta) == canview::RecordFlags::None);
    }

    const std::uint8_t frame_meta = canview::pack_frame_meta(
        canview::CanRecordKind::Frame,
        canview::RecordFlags::InvalidStruct | canview::RecordFlags::TimestampInvalid);
    NX_CHECK(canview::meta_kind(frame_meta) == canview::CanRecordKind::Frame);
    NX_CHECK(canview::has_flag(frame_meta, canview::RecordFlags::InvalidStruct));
    NX_CHECK(canview::has_flag(frame_meta, canview::RecordFlags::TimestampInvalid));
    NX_CHECK(!canview::has_flag(frame_meta, canview::RecordFlags::DuplicateReorder));

    const std::uint8_t with_reset =
        canview::with_flag(frame_meta, canview::RecordFlags::SequenceReset);
    NX_CHECK(canview::has_flag(with_reset, canview::RecordFlags::SequenceReset));
    NX_CHECK(canview::meta_kind(with_reset) == canview::CanRecordKind::Frame);

    for (const auto& row : nexigen::core::EnumTraits<canview::GapReason>::entries) {
        const std::uint8_t exact =
            canview::pack_gap_meta(canview::CanRecordKind::TransportGap, row.value, true);
        NX_CHECK(canview::meta_kind(exact) == canview::CanRecordKind::TransportGap);
        NX_CHECK(canview::meta_gap_reason(exact) == row.value);
        NX_CHECK(canview::meta_count_exact(exact));

        const std::uint8_t inexact =
            canview::pack_gap_meta(canview::CanRecordKind::IngressOverflow, row.value, false);
        NX_CHECK(canview::meta_gap_reason(inexact) == row.value);
        NX_CHECK(!canview::meta_count_exact(inexact));
    }
}

void test_sequence_tracker() {
    canview::SequenceTracker tracker{};
    hwcan::CanMessage message = make_message(hwcan::CanMessageKind::ClassicData, 8U);

    // No hardware-sequence flag: unavailable, no baseline change.
    auto observation = tracker.observe(message);
    NX_CHECK(observation.outcome == canview::SequenceOutcome::Unavailable);

    message.frame_flags.bits |= hwcan::CanFrameFlags::has_hardware_sequence;
    message.hardware_sequence = 10U;
    observation = tracker.observe(message);
    NX_CHECK(observation.outcome == canview::SequenceOutcome::First);

    message.hardware_sequence = 11U;
    observation = tracker.observe(message);
    NX_CHECK(observation.outcome == canview::SequenceOutcome::InOrder);

    message.hardware_sequence = 15U;
    observation = tracker.observe(message);
    NX_CHECK(observation.outcome == canview::SequenceOutcome::Gap);
    NX_CHECK(observation.lost_before == 3U);

    message.hardware_sequence = 15U;
    observation = tracker.observe(message);
    NX_CHECK(observation.outcome == canview::SequenceOutcome::Duplicate);

    message.hardware_sequence = 12U;
    observation = tracker.observe(message);
    NX_CHECK(observation.outcome == canview::SequenceOutcome::Reorder);

    tracker.note_controller_reset();
    message.hardware_sequence = 1U;
    observation = tracker.observe(message);
    NX_CHECK(observation.outcome == canview::SequenceOutcome::Reset);
    NX_CHECK(observation.lost_before == 0U);

    canview::SequenceTracker small{10U};
    message.hardware_sequence = 1U;
    NX_CHECK(small.observe(message).outcome == canview::SequenceOutcome::First);
    message.hardware_sequence = 12U; // missing 10, at the threshold
    observation = small.observe(message);
    NX_CHECK(observation.outcome == canview::SequenceOutcome::Gap);
    NX_CHECK(observation.lost_before == 10U);
    message.hardware_sequence = 24U; // missing 11, above the threshold
    observation = small.observe(message);
    NX_CHECK(observation.outcome == canview::SequenceOutcome::SequenceReset);
    NX_CHECK(observation.lost_before == 0U);

    small.reset();
    NX_CHECK(!small.has_baseline());
    message.hardware_sequence = 99U;
    NX_CHECK(small.observe(message).outcome == canview::SequenceOutcome::First);
}

void test_validation() {
    auto result = canview::validate_for_view(make_message(hwcan::CanMessageKind::ClassicData, 8U));
    NX_CHECK(result.valid);
    result = canview::validate_for_view(make_message(hwcan::CanMessageKind::ClassicData, 9U));
    NX_CHECK(!result.valid);
    NX_CHECK(result.reason != nullptr);

    const std::array<std::uint8_t, 9> legal{0U, 8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U};
    for (const std::uint8_t length : legal) {
        NX_CHECK(canview::is_legal_fd_length(length));
        NX_CHECK(
            canview::validate_for_view(make_message(hwcan::CanMessageKind::FdData, length)).valid);
    }
    const std::array<std::uint8_t, 3> illegal{9U, 13U, 63U};
    for (const std::uint8_t length : illegal) {
        NX_CHECK(!canview::is_legal_fd_length(length));
        NX_CHECK(
            !canview::validate_for_view(make_message(hwcan::CanMessageKind::FdData, length)).valid);
    }

    for (std::uint8_t length = 0U; length <= 8U; ++length) {
        NX_CHECK(
            canview::validate_for_view(make_message(hwcan::CanMessageKind::Remote, length)).valid);
    }
    NX_CHECK(!canview::validate_for_view(make_message(hwcan::CanMessageKind::Remote, 9U)).valid);

    NX_CHECK(
        canview::validate_for_view(make_message(hwcan::CanMessageKind::ClassicData, 8U, 0x7FFU))
            .valid);
    NX_CHECK(
        !canview::validate_for_view(make_message(hwcan::CanMessageKind::ClassicData, 8U, 0x800U))
             .valid);
    NX_CHECK(canview::validate_for_view(
                 make_message(hwcan::CanMessageKind::ClassicData, 8U, 0x1FFFFFFFU, true))
                 .valid);
    NX_CHECK(!canview::validate_for_view(
                  make_message(hwcan::CanMessageKind::ClassicData, 8U, 0x20000000U, true))
                  .valid);

    hwcan::CanMessage flagged = make_message(hwcan::CanMessageKind::ClassicData, 8U);
    flagged.frame_flags.bits =
        hwcan::CanFrameFlags::has_hardware_timestamp | hwcan::CanFrameFlags::has_hardware_sequence;
    result = canview::validate_for_view(flagged);
    NX_CHECK(result.valid);
    NX_CHECK(result.timestamp_usable);
    NX_CHECK(result.sequence_usable);

    hwcan::CanMessage bus_idle = make_message(hwcan::CanMessageKind::BusIdle, 0U, 0U);
    NX_CHECK(canview::validate_for_view(bus_idle).valid);
    hwcan::CanMessage bad_idle = make_message(hwcan::CanMessageKind::BusIdle, 0U, 1U);
    NX_CHECK(!canview::validate_for_view(bad_idle).valid);
}

void test_budget() {
    NX_CHECK(canview::capacity_for_budget(256U) == 1'048'576U);
    NX_CHECK(canview::capacity_for_budget(384U) == 2'097'152U);
    NX_CHECK(canview::capacity_for_budget(16U) == 1'024U);

    const canview::MemoryPlan full = canview::plan_for_budget(256U);
    NX_CHECK(!full.degraded);
    NX_CHECK(full.ring_capacity == 1'048'576U);
    NX_CHECK(full.overwrite_rows == 4096U);
    NX_CHECK(full.heatmap_columns == 2048U);
    NX_CHECK(full.eds_enabled);
    NX_CHECK(full.live_views == 8U);
    NX_CHECK(full.paused_snapshots == 16U);

    const canview::MemoryPlan degraded = canview::plan_for_budget(16U);
    NX_CHECK(degraded.degraded);
    NX_CHECK(degraded.ring_capacity >= 1024U);
    NX_CHECK(!degraded.eds_enabled);
    NX_CHECK(degraded.overwrite_rows < full.overwrite_rows);
    NX_CHECK(degraded.heatmap_columns < full.heatmap_columns);
    NX_CHECK(degraded.paused_snapshots <= 1U);
    NX_CHECK(!degraded.to_string().empty());
}

} // namespace

namespace canview_test {

void run_model_tests() {
    test_enums();
    test_channel_and_key();
    test_record_meta();
    test_sequence_tracker();
    test_validation();
    test_budget();
}

} // namespace canview_test
