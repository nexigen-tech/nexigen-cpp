// ex_recorder_session — full raw-session round trip over the public recorder API.
//
// Write path: StorageSettings -> schemas -> channels -> RecordShard/direct
// append -> TimeSync -> video frame -> close. Read path: SessionReader ->
// manifest -> datasets -> for_each -> recover_index -> publisher_epochs ->
// read_video -> verify. No COM, no MCAP, no media backend.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <nexigen/chrono/boot_identity.hpp>
#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/logging/log_frame.hpp>
#include <nexigen/recorder/builtin_schemas.hpp>
#include <nexigen/recorder/session_reader.hpp>
#include <nexigen/recorder/session_writer.hpp>

namespace {

namespace fs = std::filesystem;
namespace recorder = nexigen::recorder;
namespace chrono = nexigen::chrono;
namespace logging = nexigen::logging;

constexpr std::string_view kSessionId = "example";

int fail(const char* what) {
    std::fprintf(stderr, "ex_recorder_session: %s\n", what);
    return 1;
}

/// One visitor state for the whole session scan.
struct VisitState {
    std::uint64_t records{0};
    bool have_video{false};
    recorder::VideoFrameRef video{};
};

bool visit_record(void* context, const recorder::RecordHeader& header,
                  std::span<const std::byte> payload, std::uint64_t /*offset*/) {
    auto& state = *static_cast<VisitState*>(context);
    ++state.records;
    if (header.type == static_cast<std::uint16_t>(recorder::RecordType::VideoFrameRef) &&
        payload.size() == sizeof(recorder::VideoFrameRef)) {
        std::memcpy(&state.video, payload.data(), sizeof(recorder::VideoFrameRef));
        state.have_video = true;
    }
    return true;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "nexigen-example-recorder";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root, error);

    recorder::StorageSettings settings{};
    settings.session_root = root.string();
    settings.session_id = std::string(kSessionId);
    settings.max_segment_records = 10; // deterministic rotation
    settings.index_stride_bytes = 4096;
    settings.index_interval_ns = 1'000'000;
    settings.write_buffer_bytes = 8192;
    settings.index_buffer_bytes = 4096;
    settings.video_frame_buffer_bytes = 1U << 20U;
    settings.max_record_bytes = 1U << 16U;
    settings.min_free_bytes = 0;
    settings.video_crc = recorder::VideoCrcMode::PerFrame;

    const chrono::BootIdentity boot = chrono::read_boot_identity();
    char boot_text[17]{};
    settings.boot_token = std::string(chrono::format_boot_token(boot.token, boot_text));
    if (boot.uuid[0] != '\0') {
        settings.boot_uuid = std::string(boot.uuid.data());
    }

    auto writer = recorder::SessionWriter::create(settings);
    if (!writer.has_value()) {
        return fail("SessionWriter::create failed");
    }

    // Schema 1 is the built-in LogFrame descriptor; schema 2 is a small POD
    // descriptor used by the telemetry and video channels.
    for (const auto& schema : recorder::builtin_schemas()) {
        if (schema.id == 1U && !writer->declare_schema(schema).has_value()) {
            return fail("declare_schema(LogFrame) failed");
        }
    }
    recorder::SchemaDeclare pod_schema{};
    pod_schema.id = 2;
    pod_schema.name = "example.Telemetry";
    pod_schema.encoding = "jsonschema";
    const std::string pod_text =
        R"({"type":"object","fields":[{"name":"value","type":"u32","offset":0,"element_size":4,"count":1,"stride":4}]})";
    pod_schema.data.assign(pod_text.begin(), pod_text.end());
    if (!writer->declare_schema(pod_schema).has_value()) {
        return fail("declare_schema(pod) failed");
    }

    const auto log_channel =
        writer->declare_channel("log.EXMP", 1, recorder::DatasetKind::Telemetry, "nexigen.pod");
    if (!log_channel.has_value()) {
        return fail("declare_channel(log) failed");
    }
    const auto telemetry_channel = writer->declare_channel(
        "example/telemetry", 2, recorder::DatasetKind::Telemetry, "nexigen.pod");
    if (!telemetry_channel.has_value()) {
        return fail("declare_channel(telemetry) failed");
    }
    const auto video_channel =
        writer->declare_channel("example/video", 2, recorder::DatasetKind::Video, "raw");
    if (!video_channel.has_value()) {
        return fail("declare_channel(video) failed");
    }

    // Producer path: a real LogFrame through the public append().
    for (std::uint64_t i = 0; i < 3; ++i) {
        logging::LogFrame frame{};
        frame.timestamp_ns = 1'000'000U + i;
        frame.app_id[0] = 'E';
        frame.app_id[1] = 'X';
        frame.app_id[2] = 'M';
        frame.app_id[3] = 'P';
        frame.thread_id = 7;
        frame.pid = 42;
        const std::string text = "record " + std::to_string(i);
        frame.payload_size = static_cast<std::uint16_t>(text.size());
        std::memcpy(frame.payload, text.data(), text.size());
        if (!writer
                 ->append(recorder::RecordType::LogFrame, *log_channel,
                          std::as_bytes(std::span{&frame, 1U}), frame.timestamp_ns, i, 1)
                 .has_value()) {
            return fail("append(LogFrame) failed");
        }
    }

    // Producer path: three envelopes through a wait-free RecordShard.
    recorder::RecordShard shard;
    for (std::uint64_t i = 0; i < 3; ++i) {
        recorder::RecordEnvelope envelope{};
        envelope.type = recorder::RecordType::ComPayload;
        envelope.channel_id = *telemetry_channel;
        envelope.event_ns = 2'000'000U + i;
        envelope.sequence = i;
        envelope.publisher_id = 7;
        const std::uint32_t value = 42U + static_cast<std::uint32_t>(i);
        std::memcpy(envelope.payload.data(), &value, sizeof(value));
        envelope.payload_size = sizeof(value);
        if (!shard.try_push(envelope).has_value()) {
            return fail("RecordShard::try_push failed");
        }
    }
    if (!writer->append_from_shard(shard).has_value()) {
        return fail("append_from_shard failed");
    }

    // Time calibration: the converter can derive UTC without the local clock.
    std::int64_t utc_offset = 0;
    static_cast<void>(chrono::sample_utc_offset(utc_offset));
    if (!writer->append_time_sync(utc_offset).has_value()) {
        return fail("append_time_sync failed");
    }

    // Video: one 2x2 RGB24 frame; the writer aligns it to 4 KiB and fills the
    // reference fields (offset, size, CRC, segment sequence).
    recorder::VideoStreamConfig video_config{};
    video_config.codec = static_cast<std::uint16_t>(recorder::VideoCodec::RawRgb24);
    video_config.bitstream = static_cast<std::uint16_t>(recorder::BitstreamFormat::Raw);
    video_config.width = 2;
    video_config.height = 2;
    video_config.timebase_num = 1;
    video_config.timebase_den = 1'000'000'000;
    video_config.config_epoch = 1;
    recorder::VideoFrameRef video_ref{};
    video_ref.capture_mono_ns = 3'000'000;
    video_ref.source_sequence = 1;
    video_ref.config_epoch = 1;
    video_ref.width = 2;
    video_ref.height = 2;
    video_ref.codec = static_cast<std::uint16_t>(recorder::VideoCodec::RawRgb24);
    video_ref.bitstream = static_cast<std::uint16_t>(recorder::BitstreamFormat::Raw);
    std::vector<std::byte> frame(2U * 2U * 3U, std::byte{0x11});
    if (!writer->append_video(*video_channel, video_config, video_ref, frame).has_value()) {
        return fail("append_video failed");
    }

    // One explicit gap record, so inventory/gaps is exercised.
    recorder::GapMarkerPayload gap{};
    gap.first_lost_sequence = 10;
    gap.lost_count = 2;
    gap.reason = static_cast<std::uint16_t>(recorder::GapReason::IoBackpressure);
    if (!writer
             ->append(recorder::RecordType::GapMarker, *log_channel,
                      std::as_bytes(std::span{&gap, 1U}), 4'000'000, 10, 1)
             .has_value()) {
        return fail("append(GapMarker) failed");
    }

    if (!writer->close().has_value()) {
        return fail("SessionWriter::close failed");
    }

    auto reader = recorder::SessionReader::open((root / kSessionId).string());
    if (!reader.has_value()) {
        return fail("SessionReader::open failed");
    }

    const auto& manifest = reader->manifest();
    std::printf("session=%s segments=%zu schemas=%zu channels=%zu clean=%s\n",
                reader->directory().c_str(), manifest.segments.size(), manifest.schemas.size(),
                manifest.channels.size(), manifest.clean_close ? "true" : "false");
    for (const auto& segment : manifest.segments) {
        std::printf("  segment %u closed=%s records=%llu gaps=%llu rec=%llu vid=%llu\n",
                    segment.seq, segment.closed ? "true" : "false",
                    static_cast<unsigned long long>(segment.records),
                    static_cast<unsigned long long>(segment.gaps),
                    static_cast<unsigned long long>(segment.rec_bytes),
                    static_cast<unsigned long long>(segment.vid_bytes));
    }

    const auto datasets = reader->datasets();
    if (!datasets.has_value()) {
        return fail("SessionReader::datasets failed");
    }
    for (const auto& dataset : *datasets) {
        std::printf("  dataset %u %-20s kind=%s records=%llu gaps=%llu fields=%zu bytes=%llu\n",
                    dataset.channel_id, dataset.name.c_str(),
                    std::string(recorder::to_string(dataset.kind)).c_str(),
                    static_cast<unsigned long long>(dataset.records),
                    static_cast<unsigned long long>(dataset.gaps), dataset.fields.size(),
                    static_cast<unsigned long long>(dataset.payload_bytes));
    }

    VisitState state{};
    if (!reader->for_each(0, false, 0, 0, false, &visit_record, &state).has_value()) {
        return fail("SessionReader::for_each failed");
    }
    std::printf("for_each records=%llu video_ref=%s\n",
                static_cast<unsigned long long>(state.records),
                state.have_video ? "true" : "false");

    const auto recovered = reader->recover_index(0);
    if (!recovered.has_value()) {
        return fail("SessionReader::recover_index failed");
    }
    std::printf("recovered_index segment=0 entries=%zu\n", recovered->size());

    const auto epochs = reader->publisher_epochs();
    if (!epochs.has_value()) {
        return fail("SessionReader::publisher_epochs failed");
    }
    for (const auto& epoch : *epochs) {
        std::printf("  epoch channel=%u publisher=%llu first=%llu last=%llu records=%llu\n",
                    epoch.channel_id, static_cast<unsigned long long>(epoch.publisher_id),
                    static_cast<unsigned long long>(epoch.first_sequence),
                    static_cast<unsigned long long>(epoch.last_sequence),
                    static_cast<unsigned long long>(epoch.records));
    }

    if (state.have_video) {
        const auto video = reader->read_video(state.video);
        if (!video.has_value()) {
            return fail("SessionReader::read_video failed");
        }
        std::printf("video bytes=%zu first=0x%02X\n", video->size(),
                    static_cast<unsigned>(static_cast<unsigned char>((*video)[0])));
    }

    std::uint64_t records = 0;
    std::uint64_t gaps = 0;
    recorder::VerifyFailure failure{};
    const auto verified = reader->verify(records, gaps, &failure);
    if (!verified.has_value()) {
        std::fprintf(stderr, "verify failed at segment=%u offset=%llu channel=%u\n",
                     failure.segment_seq, static_cast<unsigned long long>(failure.offset),
                     failure.channel_id);
        return 1;
    }
    std::printf("verify records=%llu gaps=%llu OK\n", static_cast<unsigned long long>(records),
                static_cast<unsigned long long>(gaps));

    fs::remove_all(root, error);
    return 0;
}
