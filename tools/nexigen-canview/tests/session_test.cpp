// Session JSONL, import, export and offline replay tests.
#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hw/can/can_message.hpp>

#include "bit_activity.hpp"
#include "can_record_ring.hpp"
#include "can_view_model.hpp"
#include "heatmap.hpp"
#include "overwrite_table.hpp"
#include "record_meta.hpp"
#include "session_export.hpp"
#include "session_import.hpp"
#include "session_record.hpp"
#include "eds_library.hpp"
#include "eds_parser.hpp"
#include "session_replay.hpp"
#include "suite.hpp"
#include "support/check.hpp"
#include "time_presenter.hpp"

namespace {

namespace canview = nexigen::canview;
namespace hwcan = nexigen::hw::can;

canview::CanViewSessionHeader make_header(std::uint64_t boot_token = 42U) {
    canview::CanViewSessionHeader header{};
    header.created_ns = 1'000'000'000;
    header.boot_token = boot_token;
    header.timebase = "canview.timebase";
    canview::CanViewChannel channel{};
    channel.name = "powertrain";
    channel.channel_id = 1U;
    channel.nominal_bitrate = 500'000U;
    header.channels.push_back(channel);
    canview::SessionEdsBinding eds{};
    eds.channel_id = 1U;
    eds.node_id = 2U;
    eds.alias = "Drive";
    eds.path = "drive.eds";
    eds.hash = 77U;
    header.eds.push_back(eds);
    return header;
}

canview::SessionFrameRecord make_frame(std::uint64_t seq, std::uint64_t ts,
                                       std::uint32_t can_id = 0x100U) {
    canview::SessionFrameRecord frame{};
    frame.seq = seq;
    frame.hw_ts_ns = ts;
    frame.channel_name = "powertrain";
    frame.message.channel_id = 1U;
    frame.message.can_id = can_id;
    frame.message.dlc = 3U;
    frame.message.hardware_timestamp_ns = ts;
    frame.message.data[0] = 0x01U;
    frame.message.data[1] = 0x02U;
    frame.message.data[2] = 0xABU;
    frame.message.frame_flags.bits = hwcan::CanFrameFlags::has_hardware_timestamp;
    frame.delta_id_ns = 1'000;
    return frame;
}

void test_session_record_roundtrip() {
    auto header = make_header();
    nlohmann::json header_json = header;
    NX_CHECK(header_json["type"] == "session");
    NX_CHECK(header_json["format"] == canview::CanViewSessionHeader::format_name);
    canview::CanViewSessionHeader header_back{};
    canview::from_json(header_json, header_back);
    NX_CHECK(header_back.boot_token == 42U);
    NX_CHECK(header_back.channels.size() == 1U);
    NX_CHECK(header_back.eds.size() == 1U);
    NX_CHECK(header_back.eds[0].hash == 77U);

    auto frame = make_frame(100U, 1'000U);
    frame.utc_est_ns = 1'500U;
    frame.utc_source = "timebase";
    const nlohmann::json frame_json = frame;
    NX_CHECK(frame_json["data"] == "0102ab");
    NX_CHECK(frame_json["utc_estimated"] == true);
    canview::SessionFrameRecord frame_back{};
    canview::from_json(frame_json, frame_back);
    NX_CHECK(frame_back.hw_ts_ns == 1'000U);
    NX_CHECK(frame_back.message.dlc == 3U);
    NX_CHECK(frame_back.message.data[2] == 0xABU);
    NX_CHECK(frame_back.utc_est_ns.has_value() && *frame_back.utc_est_ns == 1'500);

    canview::SessionGapRecord gap{};
    gap.seq = 101U;
    gap.kind = canview::CanRecordKind::TransportGap;
    gap.reason = canview::GapReason::HardwareSequence;
    gap.lost = 5U;
    gap.count_exact = true;
    gap.channel_id = 1U;
    const nlohmann::json gap_json = gap;
    canview::SessionGapRecord gap_back{};
    canview::from_json(gap_json, gap_back);
    NX_CHECK(gap_back.reason == canview::GapReason::HardwareSequence);
    NX_CHECK(gap_back.lost == 5U);
    NX_CHECK(gap_back.count_exact);

    canview::SessionStatusRecord status{};
    status.channel_id = 1U;
    status.status.rx_count = 9U;
    const nlohmann::json status_json = status;
    canview::SessionStatusRecord status_back{};
    canview::from_json(status_json, status_back);
    NX_CHECK(status_back.channel_id == 1U);
    NX_CHECK(status_back.status.rx_count == 9U);

    auto message = frame.message;
    NX_CHECK(canview::payload_to_hex(message) == "0102ab");
    message.dlc = 8U;
    for (std::size_t index = 0; index < message.dlc; ++index) {
        message.data[index] = static_cast<std::uint8_t>(index);
    }
    NX_CHECK(canview::payload_to_grouped_hex(message) == "00 01 02 03  04 05 06 07");
    hwcan::CanMessage decoded{};
    NX_CHECK(canview::payload_from_hex("01 02 AB", decoded));
    NX_CHECK(decoded.dlc == 3U);
    NX_CHECK(decoded.data[2] == 0xABU);
}

std::string make_session_text() {
    auto header = make_header();
    std::string text = nlohmann::json(header).dump() + "\n";
    text += nlohmann::json(make_frame(100U, 1'000U)).dump() + "\n";
    canview::SessionGapRecord gap{};
    gap.seq = 101U;
    gap.channel_id = 1U;
    gap.kind = canview::CanRecordKind::TransportGap;
    gap.reason = canview::GapReason::HardwareSequence;
    gap.lost = 5U;
    gap.count_exact = true;
    gap.hw_ts_ns = 2'000U;
    text += nlohmann::json(gap).dump() + "\n";
    canview::SessionStatusRecord status{};
    status.seq = 102U;
    status.channel_id = 1U;
    status.status.rx_count = 10U;
    text += nlohmann::json(status).dump() + "\n";
    return text;
}

void test_import() {
    const std::string text = make_session_text();
    const auto result = canview::SessionImporter::parse(text);
    NX_CHECK(!result.legacy);
    NX_CHECK(result.records.size() == 3U);
    NX_CHECK(result.header.boot_token == 42U);
    NX_CHECK(result.header.channels.size() == 1U);
    if (result.records.size() == 3U) {
        NX_CHECK(result.records[0].type == canview::CanViewRecordType::Frame);
        NX_CHECK(result.records[0].frame.seq == 1U); // local renumbering
        NX_CHECK(result.records[1].gap.seq == 2U);
        NX_CHECK(result.records[2].status.seq == 3U);
        NX_CHECK(result.records[0].frame.message.data[2] == 0xABU);
    }

    std::string damaged = text + "not json\n";
    damaged += "{\"type\":\"frame\",\"hw_ts_ns\":3";
    auto partial = canview::SessionImporter::parse(damaged);
    NX_CHECK(partial.malformed == 1U);
    NX_CHECK(partial.partial_tail);

    const std::string legacy =
        "{\"hardware_timestamp_ns\":100,\"channel_id\":7,\"can_id\":256,\"data\":[1,2,171],"
        "\"kind\":\"ClassicData\"}\n"
        "{\"channel_id\":7,\"state\":\"Active\",\"rx_count\":1}\n";
    const auto legacy_result = canview::SessionImporter::parse(legacy);
    NX_CHECK(legacy_result.legacy);
    NX_CHECK(legacy_result.records.size() == 2U);
    NX_CHECK(legacy_result.header.channels.size() == 1U);
    if (legacy_result.records.size() == 2U) {
        NX_CHECK(legacy_result.records[0].frame.message.dlc == 3U);
        NX_CHECK(legacy_result.records[0].frame.message.data[2] == 0xABU);
        NX_CHECK(legacy_result.records[1].type == canview::CanViewRecordType::Status);
    }

    const auto line_limited = canview::SessionImporter::parse(text, 10U);
    NX_CHECK(line_limited.malformed > 0U);
    NX_CHECK(line_limited.records.empty());
    const auto limited = canview::SessionImporter::parse(text, 1024U, 1U);
    NX_CHECK(limited.records.size() == 1U);
    NX_CHECK(limited.truncated);

    std::atomic<bool> cancel{true};
    const auto cancelled = canview::SessionImporter::parse(text, 1024U, 100U, &cancel);
    NX_CHECK(cancelled.cancelled);
    NX_CHECK(cancelled.records.empty());

    const auto csv = canview::SessionImporter::parse("seq,hw_ts_ns,channel\n1,2,3\n");
    NX_CHECK(csv.unsupported_format);

    const auto* legacy_status = canview::SessionImporter::status_for(legacy_result, 7U);
    NX_CHECK(legacy_status != nullptr);
    if (legacy_status != nullptr) {
        NX_CHECK(legacy_status->status.rx_count == 1U);
    }

    auto first = make_header(1U);
    auto second = make_header(2U);
    std::string mixed = nlohmann::json(first).dump() + "\n";
    mixed += nlohmann::json(second).dump() + "\n";
    const auto mixed_result = canview::SessionImporter::parse(mixed);
    NX_CHECK(mixed_result.mixed_boot);
}

void test_export_rows() {
    auto frame = make_frame(1U, 500U, 0x200U);
    frame.co_decoded = "value,\"quoted\"\nnext";
    const std::string csv = canview::csv_row(frame);
    NX_CHECK(csv.find("\"\"quoted\"\"") != std::string::npos);
    NX_CHECK(csv.find("value,") != std::string::npos);
    NX_CHECK(!canview::csv_header().empty());
    const std::string txt = canview::txt_row(frame);
    NX_CHECK(txt.find("|") != std::string::npos);
    frame.co_decoded.clear();
    NX_CHECK(canview::txt_row(frame).find(" - ") != std::string::npos);
}

void test_async_export() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    for (std::uint64_t index = 0U; index < 3U; ++index) {
        hwcan::CanMessage message{};
        message.channel_id = 1U;
        message.can_id = 0x100U;
        message.dlc = 2U;
        message.hardware_timestamp_ns = 1'000U + index;
        const std::uint8_t meta = canview::pack_frame_meta(canview::CanRecordKind::Frame);
        static_cast<void>(ring->push(message, 0, canview::CanRecordView::no_node, meta, 0U, 0U));
    }
    canview::RingRecordSource source{*ring};
    const std::string path = "/tmp/opencode/canview-session-export.jsonl";
    canview::AsyncExportWriter writer;
    NX_CHECK(writer.start(path, canview::SessionFormat::Jsonl, make_header()).has_value());
    NX_CHECK(writer.active());
    const auto rejected = writer.start(path, canview::SessionFormat::Jsonl, make_header());
    NX_CHECK(!rejected);
    if (!rejected) {
        NX_CHECK(rejected.error() == nexigen::core::ErrorCode::Conflict);
    }
    for (std::uint64_t seq = 0U; seq < ring->tail(); ++seq) {
        NX_CHECK(writer.enqueue_from_source(source, seq, "powertrain"));
    }
    NX_CHECK(!writer.enqueue_from_source(source, 999U, "powertrain"));
    NX_CHECK(writer.progress().skipped == 1U);
    writer.finish();
    NX_CHECK(!writer.active());
    NX_CHECK(writer.progress().written == 3U);

    const auto imported = canview::SessionImporter::load(path);
    NX_CHECK(imported.records.size() == 3U);
    if (imported.records.size() == 3U) {
        NX_CHECK(imported.records[0].frame.message.dlc == 2U);
    }

    const std::string cancel_path = "/tmp/opencode/canview-session-cancel.jsonl";
    canview::AsyncExportWriter cancelled;
    NX_CHECK(
        cancelled.start(cancel_path, canview::SessionFormat::Jsonl, make_header()).has_value());
    static_cast<void>(cancelled.enqueue_from_source(source, 0U, "powertrain"));
    cancelled.cancel();
    NX_CHECK(!cancelled.active());
    NX_CHECK(!std::ifstream(cancel_path).good());
    NX_CHECK(!std::ifstream(cancel_path + ".partial").good());
}

void test_offline_canopen() {
    constexpr std::string_view eds_text = R"([1A00]
ObjectType=0x9
SubNumber=2
[1A00sub1]
DefaultValue=0x20000020
[2000]
ParameterName=DriveValue
DataType=0x0007
)";
    auto parsed = canview::EdsParser::parse(eds_text, "offline.eds");
    NX_CHECK(parsed.ok());
    if (!parsed.ok()) {
        return;
    }
    const std::uint64_t hash = parsed.dictionary->content_hash();
    const std::string path = "/tmp/opencode/canview-offline.eds";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(eds_text.data(), static_cast<std::streamsize>(eds_text.size()));
    }

    canview::ImportResult result{};
    result.header = make_header();
    result.header.eds.clear();
    canview::SessionEdsBinding binding{};
    binding.channel_id = 1U;
    binding.node_id = 2U;
    binding.alias = "Drive";
    binding.path = path;
    binding.hash = hash;
    result.header.eds.push_back(binding);

    canview::SessionFrameRecord frame = make_frame(1U, 1'000U, 0x182U);
    frame.message.dlc = 4U;
    frame.message.data = {0x78U, 0x56U, 0x34U, 0x12U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                          0U,    0U,    0U,    0U,    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                          0U,    0U,    0U,    0U,    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                          0U,    0U,    0U,    0U,    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                          0U,    0U,    0U,    0U,    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    result.records.push_back(
        canview::SessionRecord{canview::CanViewRecordType::Frame, frame, {}, {}});

    canview::EdsLibrary library;
    canview::OfflineCanopen rebuilt;
    rebuilt.build(result, &library);
    NX_CHECK(rebuilt.warnings().empty());
    NX_CHECK(!rebuilt.live_status_available());
    const auto decoded = rebuilt.decode(result.records[0]);
    NX_CHECK(decoded.source == canview::OfflineDecodeSource::Rebuilt);
    NX_CHECK(decoded.decoded.signal_count == 1U);
    NX_CHECK(!decoded.hash_mismatch);

    result.header.eds[0].hash = hash + 1U;
    canview::OfflineCanopen mismatched;
    mismatched.build(result, &library);
    NX_CHECK(!mismatched.warnings().empty());
    const auto mismatch_decoded = mismatched.decode(result.records[0]);
    NX_CHECK(mismatch_decoded.source == canview::OfflineDecodeSource::Rebuilt);
    NX_CHECK(mismatch_decoded.hash_mismatch);

    result.header.eds[0].path = "/tmp/opencode/canview-missing.eds";
    result.records[0].frame.co_decoded = "DriveValue=42";
    canview::OfflineCanopen exported;
    exported.build(result, &library);
    NX_CHECK(!exported.warnings().empty());
    const auto exported_decoded = exported.decode(result.records[0]);
    NX_CHECK(exported_decoded.source == canview::OfflineDecodeSource::Exported);
    NX_CHECK(std::string(exported_decoded.decoded.text.data()) == "DriveValue=42");
}

void test_offline_replay() {
    const auto imported = canview::SessionImporter::parse(make_session_text());
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    const auto report = canview::replay_into_ring(imported, *ring);
    NX_CHECK(report.frames == 1U);
    NX_CHECK(report.gaps == 1U);
    NX_CHECK(report.statuses == 1U);
    NX_CHECK(ring->size() == 2U);

    canview::CanViewModel model{*ring};
    const auto view_id =
        model.create_view(canview::ViewKind::All, 0U, 0U, "Offline", canview::ViewDefaults{});
    NX_CHECK(view_id.has_value());
    if (!view_id) {
        return;
    }
    model.rebuild_all();
    canview::CanView* view = model.view(*view_id);
    NX_CHECK(view != nullptr);
    if (view == nullptr) {
        return;
    }
    NX_CHECK(view->display_count() == 2U);
    view->filter().errors_only = false;
    view->filter().show_gaps = false;
    view->begin_rebuild();
    model.step_rebuilds();
    NX_CHECK(view->display_count() == 1U);

    canview::OverwriteTable overwrite;
    overwrite.init(16U);
    if (const auto frame = ring->find(0U); frame.has_value()) {
        NX_CHECK(overwrite.update(*frame, 1'000U, 0) != nullptr);
    }
    NX_CHECK(overwrite.size() == 1U);

    std::array<canview::ChannelTiming, 32> timings{};
    timings[1].nominal_bitrate = 500'000U;
    canview::HeatmapWindow heatmap;
    heatmap.init();
    heatmap.rebuild(canview::RingRecordSource{*ring}, nullptr, canview::HeatmapAxis::SharedHardware,
                    0U, 10'000U, 1U, timings);
    NX_CHECK(heatmap.column_count() > 0U);
    canview::BitActivity activity;
    activity.build(canview::RingRecordSource{*ring}, 1U, 0x100U, false);
    NX_CHECK(activity.available());
    NX_CHECK(activity.low_confidence());
    NX_CHECK(!activity.to_string().empty());

    canview::TimeContext context;
    if (const auto frame = ring->find(0U); frame.has_value()) {
        const auto cell = canview::TimePresenter::hardware(*frame, canview::TimestampMode::Driver,
                                                           *view, context);
        NX_CHECK(cell.available);
        NX_CHECK(!canview::TimePresenter::utc_ns(*frame, context).has_value());
    }
}

} // namespace

namespace canview_test {

void run_session_tests() {
    test_session_record_roundtrip();
    test_import();
    test_export_rows();
    test_async_export();
    test_offline_canopen();
    test_offline_replay();
}

} // namespace canview_test
