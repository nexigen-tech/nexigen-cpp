// nexigen::recorder — unit tests.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/recorder/dataset_info.hpp>
#include <nexigen/logging/log_frame.hpp>
#include <nexigen/metrics/metric_frame.hpp>
#include <nexigen/recorder/builtin_schemas.hpp>
#include <nexigen/recorder/config_loader.hpp>
#include <nexigen/recorder/export_selection.hpp>
#include <nexigen/recorder/payload_decoder.hpp>
#include <nexigen/core/integrity/sha256.hpp>
#include <nexigen/core/integrity/crc32c.hpp>
#include <nexigen/recorder/session_manifest.hpp>
#include <nexigen/recorder/session_reader.hpp>
#include <nexigen/recorder/session_writer.hpp>
#include <nexigen/recorder/time_base_resolver.hpp>
#include <nexigen/recorder/time_base_session_bridge.hpp>
#include <nexigen/tracing/span_frame.hpp>
#include <nexigen/recorder/wire_types.hpp>

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

[[nodiscard]] std::string temp_root() {
    return "/tmp/nexigen-recorder-tests";
}

[[nodiscard]] std::vector<std::uint8_t> json_bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

template <class E> void check_recorder_enum_vocabulary() {
    for (const auto& entry : nexigen::core::EnumTraits<E>::entries) {
        NX_CHECK(nexigen::recorder::to_string(entry.value) == entry.name);
        E parsed{};
        NX_CHECK(nexigen::recorder::from_string(entry.name, parsed));
        NX_CHECK(parsed == entry.value);
        NX_CHECK(std::format("{}", entry.value) == entry.name);
        const nlohmann::json encoded = entry.value;
        E decoded{};
        nexigen::recorder::from_json(encoded, decoded);
        NX_CHECK(decoded == entry.value);
    }
}

void test_wire_layout() {
    static_assert(sizeof(nexigen::recorder::RecordHeader) == 48U);
    static_assert(sizeof(nexigen::recorder::IndexEntry) == 64U);
    static_assert(sizeof(nexigen::recorder::VideoFrameRef) == 64U);
    static_assert(sizeof(nexigen::recorder::VideoStreamConfig) == 1056U);
    static_assert(sizeof(nexigen::recorder::SegmentFooter) == 64U);
    static_assert(sizeof(nexigen::recorder::GapMarkerPayload) == 24U);
    static_assert(sizeof(nexigen::recorder::TimeSyncPayload) == 16U);
    static_assert(sizeof(nexigen::recorder::TimeBasePayload) == 4224U);
    static_assert(alignof(nexigen::recorder::TimeBasePayload) == 64U);
    static_assert(sizeof(nexigen::chrono::TimeSnapshot) == 896U);
    NX_CHECK(nexigen::recorder::to_string(nexigen::recorder::VideoCrcMode::Off) == "Off");
    NX_CHECK(nexigen::recorder::to_string(nexigen::recorder::DatasetKind::Video) == "Video");
    NX_CHECK(nexigen::recorder::to_string(nexigen::recorder::RecordType::TimeSnapshot) ==
             "TimeSnapshot");
    NX_CHECK(static_cast<std::uint16_t>(nexigen::recorder::RecordType::TimeSnapshot) == 12U);
}

void test_settings_roundtrip() {
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = temp_root();
    settings.session_id = "roundtrip";
    settings.max_segment_bytes = 4096;
    settings.index_stride_bytes = 1024;
    settings.index_interval_ns = 10;
    settings.video_crc = nexigen::recorder::VideoCrcMode::PerFrame;
    NX_CHECK(settings.validate().has_value());
    const nlohmann::json j = settings;
    nexigen::recorder::StorageSettings decoded{};
    nexigen::recorder::from_json(j, decoded);
    NX_CHECK(decoded.session_id == settings.session_id);
    NX_CHECK(decoded.max_segment_bytes == settings.max_segment_bytes);
    NX_CHECK(decoded.video_crc == nexigen::recorder::VideoCrcMode::PerFrame);
    NX_CHECK(!settings.to_string().empty());
    nexigen::recorder::StorageSettings unsafe = settings;
    unsafe.session_layout = "../outside";
    NX_CHECK(!unsafe.validate().has_value());
    unsafe.session_layout = "{session_id}";
    NX_CHECK(unsafe.validate().has_value());
}

void test_write_read_roundtrip() {
    const std::string root = temp_root();
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_records = 50;
    settings.max_segment_bytes = 1U << 20U;
    settings.index_stride_bytes = 4096;
    settings.index_interval_ns = 1;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.video_frame_buffer_bytes = 8192;
    settings.max_record_bytes = 4096;
    settings.video_crc = nexigen::recorder::VideoCrcMode::PerFrame;

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }

    nexigen::recorder::SchemaDeclare log_schema{};
    log_schema.id = 1;
    log_schema.name = "nexigen.log.LogFrame";
    log_schema.encoding = "jsonschema";
    log_schema.units = "ns";
    log_schema.data = json_bytes(
        R"({"type":"object","fields":[{"name":"timestamp_ns","type":"u64","offset":0,"element_size":8,"count":1,"stride":8}]})");
    NX_CHECK(writer->declare_schema(log_schema).has_value());

    nexigen::recorder::SchemaDeclare video_schema{};
    video_schema.id = 2;
    video_schema.name = "nexigen.video.FrameRef";
    video_schema.encoding = "jsonschema";
    video_schema.data = json_bytes(R"({"type":"object","fields":[]})");
    NX_CHECK(writer->declare_schema(video_schema).has_value());

    nexigen::recorder::ChannelDeclare log_channel{};
    log_channel.id = 1;
    log_channel.schema_id = 1;
    log_channel.topic = "test/log";
    log_channel.kind = nexigen::recorder::DatasetKind::Telemetry;
    log_channel.message_encoding = "nexigen.pod";
    NX_CHECK(writer->declare_channel(log_channel).has_value());

    nexigen::recorder::ChannelDeclare video_channel{};
    video_channel.id = 2;
    video_channel.schema_id = 2;
    video_channel.topic = "test/camera";
    video_channel.kind = nexigen::recorder::DatasetKind::Video;
    video_channel.message_encoding = "nexigen.pod";
    NX_CHECK(writer->declare_channel(video_channel).has_value());

    NX_CHECK(writer->append_time_sync(1'700'000'000'000'000'000LL).has_value());

    std::vector<std::byte> payload(256);
    for (std::uint64_t i = 0; i < 120; ++i) {
        for (std::size_t b = 0; b < payload.size(); ++b) {
            payload[b] = static_cast<std::byte>(i + b);
        }
        NX_CHECK(writer->append(nexigen::recorder::RecordType::LogFrame, 1, payload, i * 1000, i, 7)
                     .has_value());
    }

    nexigen::recorder::VideoStreamConfig config{};
    config.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H265);
    config.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
    config.width = 1920;
    config.height = 1080;
    config.extradata_size = 3;
    config.extradata[0] = 1;
    config.extradata[1] = 2;
    config.extradata[2] = 3;
    config.config_epoch = 1;

    std::vector<std::byte> frame(5000);
    for (std::uint64_t i = 0; i < 5; ++i) {
        nexigen::recorder::VideoFrameRef reference{};
        reference.capture_mono_ns = 1'000'000 + i * 20'000'000;
        reference.source_sequence = i;
        reference.config_epoch = 1;
        reference.width = 1920;
        reference.height = 1080;
        reference.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H265);
        reference.bitstream =
            static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
        NX_CHECK(writer->append_video(2, config, reference, frame).has_value());
    }
    NX_CHECK(writer->close().has_value());

    const auto manifest_json = nlohmann::json(writer->manifest());
    nexigen::recorder::SessionManifest decoded_manifest{};
    nexigen::recorder::from_json(manifest_json, decoded_manifest);
    NX_CHECK(decoded_manifest.session_id == "sess");
    NX_CHECK(decoded_manifest.schemas.size() == 2);
    NX_CHECK(decoded_manifest.channels.size() == 2);
    NX_CHECK(decoded_manifest.clean_close);

    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    const auto datasets = reader->datasets();
    NX_CHECK(datasets.has_value());
    if (datasets.has_value()) {
        NX_CHECK(datasets->size() == 2);
        const auto& log = (*datasets)[0];
        NX_CHECK(log.channel_id == 1);
        NX_CHECK(log.records == 120);
        NX_CHECK(!log.fields.empty());
        const auto& video = (*datasets)[1];
        NX_CHECK(video.channel_id == 2);
        NX_CHECK(video.records == 5);
        NX_CHECK(video.payload_bytes == 5U * frame.size());
        NX_CHECK(video.codec == nexigen::recorder::VideoCodec::H265);
        NX_CHECK(!video.segments.empty());
    }

    std::uint64_t visited = 0;
    const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& /*header*/,
                            std::span<const std::byte> /*payload*/, std::uint64_t /*offset*/) {
        ++static_cast<std::uint64_t*>(context)[0];
        return true;
    };
    NX_CHECK(reader->for_each(1, true, 0, 0, false, visitor, &visited).has_value());
    NX_CHECK(visited == 120);

    std::vector<nexigen::recorder::VideoFrameRef> references;
    const auto ref_visitor = [](void* context, const nexigen::recorder::RecordHeader& header,
                                std::span<const std::byte> record_payload,
                                std::uint64_t /*offset*/) {
        if (header.type ==
                static_cast<std::uint16_t>(nexigen::recorder::RecordType::VideoFrameRef) &&
            record_payload.size() == sizeof(nexigen::recorder::VideoFrameRef)) {
            nexigen::recorder::VideoFrameRef reference{};
            std::memcpy(&reference, record_payload.data(), sizeof(reference));
            static_cast<std::vector<nexigen::recorder::VideoFrameRef>*>(context)->push_back(
                reference);
        }
        return true;
    };
    NX_CHECK(reader->for_each(2, true, 0, 0, false, ref_visitor, &references).has_value());
    NX_CHECK(references.size() == 5);
    std::uint64_t ranged = 0;
    const auto range_visitor = [](void* context, const nexigen::recorder::RecordHeader& /*header*/,
                                  std::span<const std::byte> /*payload*/,
                                  std::uint64_t /*offset*/) {
        ++static_cast<std::uint64_t*>(context)[0];
        return true;
    };
    NX_CHECK(reader->for_each(2, true, 50'000'000, 100'000'000, true, range_visitor, &ranged)
                 .has_value());
    NX_CHECK(ranged == 2);
    if (!references.empty()) {
        NX_CHECK(references[0].vid_offset % nexigen::recorder::kVideoAlignment == 0U);
        NX_CHECK(references[0].vid_crc32c != 0U);
        const auto bytes = reader->read_video(references[0]);
        NX_CHECK(bytes.has_value());
        if (bytes.has_value()) {
            NX_CHECK(bytes->size() == frame.size());
        }
    }

    std::uint64_t records = 0;
    std::uint64_t gaps = 0;
    NX_CHECK(reader->verify(records, gaps).has_value());
    NX_CHECK(records > 120);
    NX_CHECK(gaps == 0);

    const std::string copied = root + "/copied";
    std::filesystem::copy(root + "/sess", copied,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          error);
    NX_CHECK(!error);
    const auto copied_reader = nexigen::recorder::SessionReader::open(copied);
    NX_CHECK(copied_reader.has_value());
    if (copied_reader.has_value()) {
        const auto copied_datasets = copied_reader->datasets();
        NX_CHECK(copied_datasets.has_value() && copied_datasets->size() == 2);
    }

    if (std::getenv("NEXIGEN_RECORDER_KEEP") == nullptr) {
        std::filesystem::remove_all(root, error);
    }
}

void test_retention_and_truncation() {
    const std::string root = temp_root() + "-retention";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_records = 20;
    settings.max_segment_bytes = 1U << 20U;
    settings.retention_max_bytes = 2048;
    settings.retention = nexigen::recorder::RetentionPolicy::Circular;
    settings.index_stride_bytes = 1024;
    settings.index_interval_ns = 1;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 4096;

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Payload";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    nexigen::recorder::ChannelDeclare channel{};
    channel.id = 1;
    channel.schema_id = 1;
    channel.topic = "test/retention";
    channel.kind = nexigen::recorder::DatasetKind::Telemetry;
    channel.message_encoding = "nexigen.pod";
    NX_CHECK(writer->declare_channel(channel).has_value());

    std::vector<std::byte> payload(128);
    for (std::uint64_t i = 0; i < 100; ++i) {
        NX_CHECK(writer->append(nexigen::recorder::RecordType::LogFrame, 1, payload, i, i, 1)
                     .has_value());
    }
    NX_CHECK(writer->close().has_value());
    NX_CHECK(writer->manifest().segments.size() < 5U);

    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    std::uint64_t records = 0;
    std::uint64_t gaps = 0;
    NX_CHECK(reader->verify(records, gaps).has_value());

    const std::uint32_t segment_seq = reader->manifest().segments.front().seq;
    std::filesystem::remove(
        reader->directory() + "/" + reader->manifest().segments.front().idx_path, error);
    const auto rebuilt = reader->recover_index(segment_seq);
    NX_CHECK(rebuilt.has_value() && !rebuilt->empty());

    // Truncate the last record of the last segment and require verification to fail.
    const std::string last_rec = reader->rec_path(reader->manifest().segments.back().seq);
    std::filesystem::resize_file(last_rec, std::filesystem::file_size(last_rec, error) - 20U,
                                 error);
    NX_CHECK(!error);
    std::uint64_t trunc_records = 0;
    NX_CHECK(!reader->verify(trunc_records, gaps).has_value());
    std::filesystem::remove_all(root, error);
}

void test_video_gap() {
    const std::string root = temp_root() + "-gap";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U << 20U;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.video_frame_buffer_bytes = 8192;
    settings.max_record_bytes = 4096;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Video";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    nexigen::recorder::SchemaDeclare schema2{};
    schema2.id = 2;
    schema2.name = "test.VideoV2";
    schema2.encoding = "jsonschema";
    schema2.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema2).has_value());
    const auto first_id = writer->declare_channel(
        "test/camera", 1, nexigen::recorder::DatasetKind::Video, "nexigen.pod");
    NX_CHECK(first_id.has_value() && *first_id == 1);
    const auto second_id = writer->declare_channel(
        "test/camera", 2, nexigen::recorder::DatasetKind::Video, "nexigen.pod");
    NX_CHECK(second_id.has_value() && *second_id == 2);
    nexigen::recorder::VideoStreamConfig config{};
    config.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H265);
    config.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
    config.width = 1920;
    config.height = 1080;
    config.config_epoch = 1;
    std::vector<std::byte> frame(1024);
    for (const std::uint64_t sequence : {0ULL, 2ULL}) {
        nexigen::recorder::VideoFrameRef reference{};
        reference.source_sequence = sequence;
        reference.capture_mono_ns = sequence * 20'000'000;
        reference.config_epoch = 1;
        NX_CHECK(writer->append_video(1, config, reference, frame).has_value());
    }
    NX_CHECK(writer->close().has_value());
    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (reader.has_value()) {
        std::uint64_t records = 0;
        std::uint64_t gaps = 0;
        NX_CHECK(reader->verify(records, gaps).has_value());
        NX_CHECK(gaps == 1);
        const auto datasets = reader->datasets();
        NX_CHECK(datasets.has_value());
        if (datasets.has_value()) {
            const auto found = std::find_if(
                datasets->begin(), datasets->end(),
                [](const nexigen::recorder::DatasetInfo& info) { return info.channel_id == 1; });
            NX_CHECK(found != datasets->end());
            if (found != datasets->end()) {
                NX_CHECK(found->kind == nexigen::recorder::DatasetKind::Video);
                NX_CHECK(found->codec == nexigen::recorder::VideoCodec::H265);
                NX_CHECK(found->bitstream == nexigen::recorder::BitstreamFormat::AnnexB);
                NX_CHECK(found->width == 1920);
                NX_CHECK(found->height == 1080);
                NX_CHECK(found->gaps == 1);
                NX_CHECK(found->closed);
                NX_CHECK(found->config_epochs.size() == 1 && found->config_epochs[0] == 1);
                NX_CHECK(found->segments.size() == 1 && found->segments[0].closed);
                NX_CHECK(found->segments[0].gaps == 1);
            }
        }
    }
    std::filesystem::remove_all(root, error);
}

/// @brief D10: `max_segment_bytes` covers `.nxrec` + `.nxvid`, not `.nxrec` alone.
void test_video_rotation_bytes() {
    const std::string root = temp_root() + "-video-rotation";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 64U * 1024U;
    settings.max_segment_duration_ns = 3600ULL * 1'000'000'000ULL;
    settings.max_segment_records = 0; // bytes alone must drive rotation
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 4096;
    settings.index_interval_ns = 1;
    settings.video_frame_buffer_bytes = 64U * 1024U;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = 0;

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Video";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/camera", 1, nexigen::recorder::DatasetKind::Video, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    nexigen::recorder::VideoStreamConfig config{};
    config.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H265);
    config.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
    config.width = 64;
    config.height = 64;
    config.config_epoch = 1;
    // 16 KiB per frame: four frames overshoot the 64 KiB cap once video bytes
    // count, while the `.nxrec` side stays far below it.
    const std::vector<std::byte> frame(16U * 1024U, std::byte{0x5A});
    for (std::uint64_t i = 0; i < 8; ++i) {
        nexigen::recorder::VideoFrameRef reference{};
        reference.capture_mono_ns = i * 20'000'000;
        reference.source_sequence = i;
        reference.config_epoch = 1;
        NX_CHECK(writer->append_video(*channel, config, reference, frame).has_value());
    }
    NX_CHECK(writer->close().has_value());

    const auto& segments = writer->manifest().segments;
    NX_CHECK(segments.size() >= 2U);
    std::uint64_t vid_total = 0;
    std::uint64_t rec_total = 0;
    for (const auto& segment : segments) {
        vid_total += segment.vid_bytes;
        rec_total += segment.rec_bytes;
        // One frame may overshoot the cap, but never a whole extra frame.
        NX_CHECK(segment.rec_bytes + segment.vid_bytes <=
                 settings.max_segment_bytes + frame.size());
    }
    NX_CHECK(vid_total == frame.size() * 8U);
    // Without counting `.nxvid`, the 128 KiB run would stay in one segment.
    NX_CHECK(rec_total < settings.max_segment_bytes);
    std::filesystem::remove_all(root, error);
}

/// @brief Video references name the segment `seq` and stay readable after
///        circular retention has erased the leading segments.
void test_video_refs_survive_retention() {
    const std::string root = temp_root() + "-video-retention";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U << 20U;
    settings.max_segment_records = 3;
    settings.retention_max_bytes = 4096;
    settings.retention = nexigen::recorder::RetentionPolicy::Circular;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 1024;
    settings.index_interval_ns = 1;
    settings.video_frame_buffer_bytes = 8192;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = 0;

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Video";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/camera", 1, nexigen::recorder::DatasetKind::Video, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    nexigen::recorder::VideoStreamConfig config{};
    config.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H265);
    config.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
    config.width = 64;
    config.height = 64;
    config.config_epoch = 1;
    const std::vector<std::byte> frame(512, std::byte{0x42});
    for (std::uint64_t i = 0; i < 12; ++i) {
        nexigen::recorder::VideoFrameRef reference{};
        reference.capture_mono_ns = i * 20'000'000;
        reference.source_sequence = i;
        reference.config_epoch = 1;
        NX_CHECK(writer->append_video(*channel, config, reference, frame).has_value());
    }
    NX_CHECK(writer->close().has_value());

    const auto& kept = writer->manifest().segments;
    NX_CHECK(!kept.empty());
    NX_CHECK(kept.front().seq > 0U); // retention removed the leading segments
    if (kept.empty()) {
        return;
    }

    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    struct VideoRefState {
        const nexigen::recorder::SessionReader* reader;
        std::vector<std::uint32_t> segment_seqs;
        std::size_t refs;
        std::size_t read_failures;
    } state{&*reader, {}, 0, 0};
    const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& header,
                            std::span<const std::byte> payload, std::uint64_t /*offset*/) {
        auto& s = *static_cast<VideoRefState*>(context);
        if (header.type !=
            static_cast<std::uint16_t>(nexigen::recorder::RecordType::VideoFrameRef)) {
            return true;
        }
        nexigen::recorder::VideoFrameRef reference{};
        std::memcpy(&reference, payload.data(), sizeof(reference));
        ++s.refs;
        s.segment_seqs.push_back(reference.vid_segment_seq);
        if (!s.reader->read_video(reference).has_value()) {
            ++s.read_failures;
        }
        return true;
    };
    NX_CHECK(reader->for_each(0, false, 0, 0, false, visitor, &state).has_value());
    NX_CHECK(state.refs > 0U);
    NX_CHECK(state.read_failures == 0U);
    for (const auto seq : state.segment_seqs) {
        const bool belongs = std::any_of(
            kept.begin(), kept.end(),
            [seq](const nexigen::recorder::SegmentInfo& segment) { return segment.seq == seq; });
        NX_CHECK(belongs);
    }
    std::filesystem::remove_all(root, error);
}

/// @brief Hardware CRC bodies must agree with the portable table at every
///        length and alignment the writer can see.
void test_crc32c_hardware_matches_table() {
    const std::string root = temp_root() + "-crc";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 1;
    settings.index_interval_ns = 1;
    settings.max_record_bytes = 8192;
    settings.min_free_bytes = 0;

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Crc";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/crc", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::array<std::size_t, 24> sizes{0U,  1U,  2U,  3U,   4U,   5U,   6U,    7U,
                                            8U,  9U,  15U, 16U,  17U,  31U,  32U,   33U,
                                            63U, 64U, 65U, 127U, 255U, 511U, 1024U, 4096U};
    for (std::uint64_t i = 0; i < sizes.size(); ++i) {
        std::vector<std::byte> payload(sizes[i]);
        for (std::size_t b = 0; b < payload.size(); ++b) {
            payload[b] = static_cast<std::byte>((b * 31U + i * 7U) & 0xffU);
        }
        NX_CHECK(
            writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload, i, i, 1)
                .has_value());
    }
    NX_CHECK(writer->close().has_value());

    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    struct CrcState {
        std::size_t records;
        std::size_t mismatches;
    } state{0, 0};
    const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& header,
                            std::span<const std::byte> payload, std::uint64_t /*offset*/) {
        auto& s = *static_cast<CrcState*>(context);
        ++s.records;
        const auto payload_bytes = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());
        if (nexigen::core::integrity::crc32c(payload_bytes) != header.payload_crc32c) {
            ++s.mismatches;
        }
        const auto header_bytes =
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(&header),
                                          offsetof(nexigen::recorder::RecordHeader, header_crc32c));
        if (nexigen::core::integrity::crc32c(header_bytes) != header.header_crc32c) {
            ++s.mismatches;
        }
        return true;
    };
    NX_CHECK(reader->for_each(*channel, true, 0, 0, false, visitor, &state).has_value());
    NX_CHECK(state.records == sizes.size());
    NX_CHECK(state.mismatches == 0U);
    std::filesystem::remove_all(root, error);
}

/// @brief `IndexEntry.channel_mask` names the channels of the interval.
void test_index_channel_mask() {
    const std::string root = temp_root() + "-channel-mask";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 1;
    settings.index_interval_ns = 1;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = 0;

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Mask";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto first = writer->declare_channel(
        "test/one", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    const auto second = writer->declare_channel(
        "test/two", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    NX_CHECK(first.has_value() && second.has_value());
    if (!first.has_value() || !second.has_value()) {
        return;
    }
    const std::vector<std::byte> payload(8);
    for (std::uint32_t id : {*first, *second, *first}) {
        NX_CHECK(writer->append(nexigen::recorder::RecordType::ComPayload, id, payload, id, id, 1)
                     .has_value());
    }
    NX_CHECK(writer->close().has_value());

    const auto& segment = writer->manifest().segments.front();
    std::ifstream index(root + "/sess/" + segment.idx_path, std::ios::binary);
    NX_CHECK(index.good());
    std::uint64_t mask_union = 0;
    nexigen::recorder::IndexEntry entry{};
    while (index.read(reinterpret_cast<char*>(&entry), sizeof(entry))) {
        NX_CHECK((entry.channel_mask & 1ULL) == 0U); // channel 0 is metadata-only
        mask_union |= entry.channel_mask;
    }
    NX_CHECK((mask_union & (1ULL << 1U)) != 0U);
    NX_CHECK((mask_union & (1ULL << 2U)) != 0U);
    std::filesystem::remove_all(root, error);
}

/// @brief `min_free_bytes` halts rotation cleanly and keeps closed triplets.
void test_low_disk_rotation() {
    const std::string root = temp_root() + "-low-disk";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U; // first data append must try to rotate
    settings.max_segment_duration_ns = 3600ULL * 1'000'000'000ULL;
    settings.max_segment_records = 0;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 4096;
    settings.index_interval_ns = 1;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = std::numeric_limits<std::uint64_t>::max();

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Disk";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/disk", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::vector<std::byte> payload(16);
    const auto blocked =
        writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload, 0U, 0U, 1);
    NX_CHECK(!blocked.has_value());
    NX_CHECK(blocked.error() == nexigen::core::ErrorCode::ResourceExhausted);
    NX_CHECK(writer->manifest().clean_close);
    NX_CHECK(writer->manifest().segments.size() == 1U);
    NX_CHECK(writer->manifest().segments.front().closed);
    NX_CHECK(
        !writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload, 3U, 3U, 1)
             .has_value());
    const auto& segment = writer->manifest().segments.front();
    NX_CHECK(std::filesystem::exists(root + "/sess/" + segment.rec_path));
    NX_CHECK(std::filesystem::exists(root + "/sess/" + segment.vid_path));
    std::filesystem::remove_all(root, error);
}

/// @brief Sparse index intervals bound out-of-order event times.
void test_out_of_order_events() {
    const std::string root = temp_root() + "-out-of-order";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 1;
    settings.index_interval_ns = 1;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = 0;

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Order";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/order", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::array<std::uint64_t, 4> events{100U, 50U, 150U, 75U};
    for (std::uint64_t i = 0; i < events.size(); ++i) {
        // `event_of()` takes the event time from VideoFrameRef.capture_mono_ns;
        // other record types are bounded by commit time.
        nexigen::recorder::VideoFrameRef reference{};
        reference.capture_mono_ns = events[i];
        reference.source_sequence = i;
        NX_CHECK(
            writer
                ->append(nexigen::recorder::RecordType::VideoFrameRef, *channel,
                         std::span<const std::byte>(reinterpret_cast<const std::byte*>(&reference),
                                                    sizeof(reference)),
                         events[i], i, 1)
                .has_value());
    }
    NX_CHECK(writer->close().has_value());

    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    std::size_t count = 0;
    const auto visitor = [](void* context, const nexigen::recorder::RecordHeader&,
                            std::span<const std::byte>, std::uint64_t) {
        ++*static_cast<std::size_t*>(context);
        return true;
    };
    NX_CHECK(reader->for_each(*channel, true, 0, 120, true, visitor, &count).has_value());
    NX_CHECK(count == 3U);
    count = 0;
    NX_CHECK(reader->for_each(*channel, true, 60, 160, true, visitor, &count).has_value());
    NX_CHECK(count == 3U);
    count = 0;
    NX_CHECK(reader->for_each(*channel, true, 40, 60, true, visitor, &count).has_value());
    NX_CHECK(count == 1U);
    std::filesystem::remove_all(root, error);
}

/// @brief Frames larger than the record write buffer are written whole; the
///        declared frame limit is reported before recording.
void test_large_video_frame() {
    const std::string root = temp_root() + "-large-frame";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 4096;
    settings.index_interval_ns = 1;
    settings.video_frame_buffer_bytes = 64U * 1024U;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = 0;

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Large";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/large", 1, nexigen::recorder::DatasetKind::Video, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    nexigen::recorder::VideoStreamConfig config{};
    config.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::RawRgb24);
    config.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::Raw);
    config.width = 64;
    config.height = 64;
    config.config_epoch = 1;
    std::vector<std::byte> frame(16U * 1024U, std::byte{0x3C});
    nexigen::recorder::VideoFrameRef reference{};
    reference.capture_mono_ns = 1;
    reference.source_sequence = 1;
    reference.config_epoch = 1;
    NX_CHECK(writer->append_video(*channel, config, reference, frame).has_value());

    std::vector<std::byte> oversized(128U * 1024U);
    NX_CHECK(!writer->append_video(*channel, config, reference, oversized).has_value());
    NX_CHECK(writer->close().has_value());

    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    auto collected = reader->datasets();
    NX_CHECK(collected.has_value());
    if (!collected.has_value()) {
        return;
    }
    struct FrameState {
        const nexigen::recorder::SessionReader* reader;
        std::size_t frames;
    } state{&*reader, 0};
    const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& header,
                            std::span<const std::byte> payload, std::uint64_t /*offset*/) {
        auto& s = *static_cast<FrameState*>(context);
        if (header.type !=
            static_cast<std::uint16_t>(nexigen::recorder::RecordType::VideoFrameRef)) {
            return true;
        }
        nexigen::recorder::VideoFrameRef ref{};
        std::memcpy(&ref, payload.data(), sizeof(ref));
        const auto bytes = s.reader->read_video(ref);
        NX_CHECK(bytes.has_value());
        if (bytes.has_value()) {
            NX_CHECK(bytes->size() == 16U * 1024U);
            NX_CHECK((*bytes)[0] == std::byte{0x3C});
        }
        NX_CHECK(ref.vid_offset % 4096U == 0U);
        ++s.frames;
        return true;
    };
    NX_CHECK(reader->for_each(0, false, 0, 0, false, visitor, &state).has_value());
    NX_CHECK(state.frames == 1U);
    std::filesystem::remove_all(root, error);
}

/// @brief A new config epoch resets the sequence without emitting a gap.
void test_encoder_restart_not_gap() {
    const std::string root = temp_root() + "-encoder-restart";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 4096;
    settings.index_interval_ns = 1;
    settings.video_frame_buffer_bytes = 8192;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = 0;

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Restart";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/restart", 1, nexigen::recorder::DatasetKind::Video, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::vector<std::byte> frame(256, std::byte{0x11});
    for (std::uint64_t epoch = 1; epoch <= 2; ++epoch) {
        nexigen::recorder::VideoStreamConfig config{};
        config.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H265);
        config.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
        config.width = 64;
        config.height = 64;
        config.config_epoch = epoch;
        for (std::uint64_t sequence = 0; sequence < 3; ++sequence) {
            nexigen::recorder::VideoFrameRef reference{};
            reference.capture_mono_ns = epoch * 1000U + sequence;
            reference.source_sequence = sequence;
            reference.config_epoch = epoch;
            NX_CHECK(writer->append_video(*channel, config, reference, frame).has_value());
        }
    }
    NX_CHECK(writer->close().has_value());

    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    std::uint64_t records = 0;
    std::uint64_t gaps = 0;
    NX_CHECK(reader->verify(records, gaps).has_value());
    NX_CHECK(gaps == 0U);
    auto datasets = reader->datasets();
    NX_CHECK(datasets.has_value() && datasets->size() == 1U);
    if (datasets.has_value() && !datasets->empty()) {
        NX_CHECK((*datasets)[0].config_epochs.size() == 2U);
        NX_CHECK((*datasets)[0].gaps == 0U);
    }
    std::filesystem::remove_all(root, error);
}

/// @brief Every video reference names the sequence of the segment it lives in,
///        also when a single `.nxrec` is opened without the manifest.
void test_multi_segment_video_refs() {
    const std::string root = temp_root() + "-multi-video";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_records = 4;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 4096;
    settings.index_interval_ns = 1;
    settings.video_frame_buffer_bytes = 8192;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = 0;

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Segments";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/camera", 1, nexigen::recorder::DatasetKind::Video, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    nexigen::recorder::VideoStreamConfig config{};
    config.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H265);
    config.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
    config.width = 64;
    config.height = 64;
    config.config_epoch = 1;
    const std::vector<std::byte> frame(512, std::byte{0x27});
    for (std::uint64_t i = 0; i < 12; ++i) {
        nexigen::recorder::VideoFrameRef reference{};
        reference.capture_mono_ns = i * 20'000'000;
        reference.source_sequence = i;
        reference.config_epoch = 1;
        NX_CHECK(writer->append_video(*channel, config, reference, frame).has_value());
    }
    NX_CHECK(writer->close().has_value());

    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    NX_CHECK(reader->manifest().segments.size() > 1U);
    std::size_t segments_checked = 0;
    std::size_t refs_checked = 0;
    for (const auto& segment : reader->manifest().segments) {
        if (segment.vid_bytes == 0U) {
            continue;
        }
        const auto isolated = nexigen::recorder::SessionReader::open_segment(
            reader->directory() + "/" + segment.rec_path);
        NX_CHECK(isolated.has_value());
        if (!isolated.has_value()) {
            continue;
        }
        NX_CHECK(isolated->manifest().segments.front().seq == segment.seq);
        ++segments_checked;
        struct RefState {
            const nexigen::recorder::SessionReader* reader;
            std::uint32_t expected_seq;
            std::size_t refs;
        } state{&*isolated, segment.seq, 0};
        const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& header,
                                std::span<const std::byte> payload, std::uint64_t /*offset*/) {
            auto& s = *static_cast<RefState*>(context);
            if (header.type !=
                static_cast<std::uint16_t>(nexigen::recorder::RecordType::VideoFrameRef)) {
                return true;
            }
            nexigen::recorder::VideoFrameRef ref{};
            std::memcpy(&ref, payload.data(), sizeof(ref));
            NX_CHECK(ref.vid_segment_seq == s.expected_seq);
            NX_CHECK(s.reader->read_video(ref).has_value());
            ++s.refs;
            return true;
        };
        NX_CHECK(isolated->for_each(0, false, 0, 0, false, visitor, &state).has_value());
        refs_checked += state.refs;
    }
    NX_CHECK(segments_checked > 1U);
    NX_CHECK(refs_checked > 0U);
    std::filesystem::remove_all(root, error);
}

void test_builtin_frame_descriptors() {
    using namespace nexigen;
    static_assert(sizeof(logging::LogFrame) == 256U);
    static_assert(offsetof(logging::LogFrame, payload) == 45U);
    static_assert(sizeof(metrics::MetricFrame) == 4488U);
    static_assert(offsetof(metrics::MetricFrame, record) == 32U);
    static_assert(offsetof(metrics::MetricRecord, kind) == 256U);
    static_assert(offsetof(metrics::MetricRecord, scalar) == 264U);
    static_assert(sizeof(tracing::SpanFrame) == 384U);
    static_assert(offsetof(tracing::SpanFrame, name) == 72U);

    const auto schemas = recorder::builtin_schemas();
    auto find = [&schemas](std::string_view name) -> const recorder::SchemaDeclare* {
        for (const auto& schema : schemas) {
            if (schema.name == name) {
                return &schema;
            }
        }
        return nullptr;
    };
    auto fields_of = [](const recorder::SchemaDeclare& schema) {
        return nlohmann::json::parse(schema.data.begin(), schema.data.end()).at("fields");
    };
    auto has_field = [](const nlohmann::json& fields, std::string_view name, std::size_t offset,
                        std::size_t count) {
        for (const auto& field : fields) {
            if (field.at("name") == name) {
                return field.at("offset").get<std::size_t>() == offset &&
                       field.at("count").get<std::size_t>() == count;
            }
        }
        return false;
    };

    const recorder::SchemaDeclare* log = find("nexigen.log.LogFrame");
    NX_CHECK(log != nullptr);
    const auto log_fields = fields_of(*log);
    NX_CHECK(has_field(log_fields, "app_id", 8U, 16U));
    NX_CHECK(has_field(log_fields, "payload", 45U, 211U));

    const recorder::SchemaDeclare* metric = find("nexigen.metrics.MetricFrame");
    NX_CHECK(metric != nullptr);
    const auto metric_fields = fields_of(*metric);
    NX_CHECK(has_field(metric_fields, "app_id", 0U, 16U));
    NX_CHECK(has_field(metric_fields, "meter_name", 32U, 64U));
    NX_CHECK(has_field(metric_fields, "kind", 288U, 1U));
    NX_CHECK(has_field(metric_fields, "statistics_mean", 320U, 1U));
    NX_CHECK(has_field(metric_fields, "histogram_buckets_count", 2440U, 256U));

    const recorder::SchemaDeclare* span = find("nexigen.tracing.SpanFrame");
    NX_CHECK(span != nullptr);
    const auto span_fields = fields_of(*span);
    NX_CHECK(has_field(span_fields, "name", 72U, 48U));
    NX_CHECK(has_field(span_fields, "app_id", 312U, 16U));
}

void test_builtins_and_config() {
    const auto schemas = nexigen::recorder::builtin_schemas();
    NX_CHECK(schemas.size() == 9);
    bool log_found = false;
    bool time_base_found = false;
    bool time_snapshot_found = false;
    for (const auto& schema : schemas) {
        if (schema.name == "nexigen.log.LogFrame") {
            log_found = true;
            NX_CHECK(!schema.data.empty());
            NX_CHECK(schema.data_crc32c != 0U);
        }
        if (schema.name == "nexigen.recorder.TimeBase") {
            time_base_found = true;
            NX_CHECK(!schema.data.empty());
            NX_CHECK(schema.data_crc32c == nexigen::core::integrity::crc32c(schema.data));
        }
        if (schema.name == "nexigen.recorder.TimeSnapshot") {
            time_snapshot_found = true;
            NX_CHECK(!schema.data.empty());
            NX_CHECK(schema.data_crc32c == nexigen::core::integrity::crc32c(schema.data));
        }
    }
    NX_CHECK(log_found);
    NX_CHECK(time_base_found);
    NX_CHECK(time_snapshot_found);

    const std::string root = temp_root() + "-config";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::string schema_path = root + "/schema.json";
    {
        std::ofstream output(schema_path);
        output << R"({"type":"object","fields":[]})";
    }
    const std::string digest = nexigen::core::integrity::sha256_file(schema_path).value_or("");
    NX_CHECK(!digest.empty());
    const std::string config_path = root + "/recorder.json";
    {
        std::ofstream output(config_path);
        output << R"({"storage":{"session_root":")" << root
               << R"(","session_id":"cfg","format":"Raw","time_domain":"MonotonicRaw",)"
               << R"("max_segment_bytes":1048576,"max_segment_duration_ns":1000000000,)"
               << R"("index_stride_bytes":65536,"index_interval_ns":1000000,)"
               << R"("write_buffer_bytes":65536,"index_buffer_bytes":4096,)"
               << R"("video_frame_buffer_bytes":65536,"max_record_bytes":4096},)"
               << R"("schemas":[{"id":1,"name":"test","version":1,"encoding":"jsonschema",)"
               << R"("path":")" << schema_path << R"(","sha256":")" << digest << R"("}],)"
               << R"("channels":[{"id":1,"schema_id":1,"topic":"test/log",)"
               << R"("kind":"Telemetry","message_encoding":"nexigen.pod"}]})";
    }
    const auto config = nexigen::recorder::load_recorder_config(config_path);
    NX_CHECK(config.has_value());
    if (config.has_value()) {
        NX_CHECK(config->schemas.size() == 1);
        NX_CHECK(config->channels.size() == 1);
        NX_CHECK(config->schemas[0].data.size() > 0);
    }
    std::filesystem::remove_all(root, error);
}

void test_torn_every_byte() {
    const std::string root = temp_root() + "-torn";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U << 20U;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 4096;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Torn";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    nexigen::recorder::ChannelDeclare channel{};
    channel.id = 1;
    channel.schema_id = 1;
    channel.topic = "test/torn";
    channel.kind = nexigen::recorder::DatasetKind::Telemetry;
    channel.message_encoding = "nexigen.pod";
    NX_CHECK(writer->declare_channel(channel).has_value());
    std::vector<std::byte> payload(64);
    for (std::uint64_t i = 0; i < 3; ++i) {
        NX_CHECK(writer->append(nexigen::recorder::RecordType::LogFrame, 1, payload, i, i, 1)
                     .has_value());
    }
    NX_CHECK(writer->close().has_value());
    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    const std::string path = reader->rec_path(0);
    std::ifstream input(path, std::ios::binary);
    const std::vector<char> original((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
    bool ok = true;
    for (std::size_t length = 0; length <= original.size(); ++length) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(original.data(), static_cast<std::streamsize>(length));
        output.close();
        std::uint64_t visited = 0;
        const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& /*header*/,
                                std::span<const std::byte> /*payload*/, std::uint64_t /*offset*/) {
            ++static_cast<std::uint64_t*>(context)[0];
            return true;
        };
        const auto status = reader->for_each(1, true, 0, 0, false, visitor, &visited);
        if (!status.has_value() || visited > 3U) {
            ok = false;
            break;
        }
    }
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(original.data(), static_cast<std::streamsize>(original.size()));
    }
    NX_CHECK(ok);

    bool hostile_ok = false;
    for (std::size_t i = 0; i + sizeof(nexigen::recorder::RecordHeader) <= original.size(); ++i) {
        if (static_cast<std::uint8_t>(original[i]) == 0x31U &&
            static_cast<std::uint8_t>(original[i + 1]) == 0x44U &&
            static_cast<std::uint8_t>(original[i + 2]) == 0x43U &&
            static_cast<std::uint8_t>(original[i + 3]) == 0x52U) {
            std::vector<char> mutated = original;
            nexigen::recorder::RecordHeader header{};
            std::memcpy(&header, mutated.data() + i, sizeof(header));
            header.payload_size = 0xFFFFFFFFU;
            header.header_crc32c = nexigen::core::integrity::crc32c(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(&header), 40U));
            std::memcpy(mutated.data() + i, &header, sizeof(header));
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(mutated.data(), static_cast<std::streamsize>(mutated.size()));
            output.close();
            std::uint64_t visited = 0;
            const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& /*h*/,
                                    std::span<const std::byte> /*p*/, std::uint64_t /*o*/) {
                ++static_cast<std::uint64_t*>(context)[0];
                return true;
            };
            const auto status = reader->for_each(1, true, 0, 0, false, visitor, &visited);
            hostile_ok = !status.has_value();
            break;
        }
    }
    NX_CHECK(hostile_ok);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(original.data(), static_cast<std::streamsize>(original.size()));
    }
    std::filesystem::remove_all(root, error);
}

void test_shard() {
    const std::string root = temp_root() + "-shard";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U << 20U;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 4096;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Shard";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    NX_CHECK(writer
                 ->declare_channel("test/shard", 1, nexigen::recorder::DatasetKind::Telemetry,
                                   "nexigen.pod")
                 .has_value());
    nexigen::recorder::RecordShard shard{};
    for (std::uint64_t i = 0; i < 3; ++i) {
        nexigen::recorder::RecordEnvelope envelope{};
        envelope.type = nexigen::recorder::RecordType::LogFrame;
        envelope.channel_id = 1;
        envelope.payload_size = 16;
        envelope.event_ns = i;
        envelope.sequence = i;
        NX_CHECK(shard.try_push(envelope).has_value());
    }
    NX_CHECK(writer->append_from_shard(shard).has_value());
    NX_CHECK(writer->close().has_value());
    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (reader.has_value()) {
        std::uint64_t records = 0;
        std::uint64_t gaps = 0;
        NX_CHECK(reader->verify(records, gaps).has_value());
        NX_CHECK(records >= 3);
    }
    std::filesystem::remove_all(root, error);
}

void test_payload_decoder() {
    using nexigen::recorder::DatasetField;
    using nexigen::recorder::PayloadKind;

    // JSON payloads decode directly.
    const std::string json_text = R"({"speed":12.5})";
    const auto json_bytes_span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(json_text.data()), json_text.size());
    const auto json_view = nexigen::recorder::decode_payload("json", {}, {}, json_bytes_span);
    NX_CHECK(json_view.has_value());
    if (json_view.has_value()) {
        NX_CHECK(json_view->kind == PayloadKind::Json);
        NX_CHECK(json_view->text.find("speed") != std::string::npos);
    }

    // A field layout decodes fixed-offset POD values.
    DatasetField value{};
    value.name = "value";
    value.type = "u16";
    value.offset = 0;
    value.element_size = 2;
    value.count = 1;
    value.stride = 2;
    DatasetField label{};
    label.name = "label";
    label.type = "char[4]";
    label.offset = 2;
    label.element_size = 1;
    label.count = 4;
    label.stride = 1;
    std::array<std::byte, 6> pod{std::byte{0x34}, std::byte{0x12}, std::byte{'o'},
                                 std::byte{'k'},  std::byte{0},    std::byte{0}};
    const auto pod_view = nexigen::recorder::decode_payload(
        "jsonschema", {}, std::vector<DatasetField>{value, label}, pod);
    NX_CHECK(pod_view.has_value());
    if (pod_view.has_value()) {
        NX_CHECK(pod_view->kind == PayloadKind::Json);
        const auto document = nlohmann::json::parse(pod_view->text, nullptr, false);
        NX_CHECK(!document.is_discarded());
        NX_CHECK(document["value"] == 0x1234);
        NX_CHECK(document["label"] == "ok");
    }

    // Truncated POD payloads are rejected rather than guessed.
    const auto short_pod =
        nexigen::recorder::decode_payload("jsonschema", {}, std::vector<DatasetField>{value},
                                          std::span<const std::byte>(pod).first(1));
    NX_CHECK(!short_pod.has_value());

    // A minimal FileDescriptorSet passes descriptor validation and stays opaque.
    const std::array<std::uint8_t, 14> descriptor{0x0AU, 0x0CU, 0x0AU, 0x0AU, 't', 'e', 's',
                                                  't',   '.',   'p',   'r',   'o', 't', 'o'};
    std::array<std::byte, 3> message{std::byte{0x08}, std::byte{0x2A}, std::byte{0x00}};
    const auto proto_view = nexigen::recorder::decode_payload("protobuf", descriptor, {}, message);
    NX_CHECK(proto_view.has_value());
    if (proto_view.has_value()) {
        NX_CHECK(proto_view->kind == PayloadKind::Opaque);
        NX_CHECK(!proto_view->text.empty());
    }
    const std::array<std::uint8_t, 4> bad_descriptor{0xFFU, 0xFFU, 0xFFU, 0xFFU};
    NX_CHECK(
        !nexigen::recorder::decode_payload("protobuf", bad_descriptor, {}, message).has_value());

    // A structurally valid .bfbs schema also passes through opaque.
    const std::array<std::uint8_t, 20> bfbs{0x0CU, 0x00U, 0x00U, 0x00U, 0x04U, 0x00U, 0x08U,
                                            0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x08U, 0x00U,
                                            0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    const auto bfbs_view = nexigen::recorder::decode_payload("flatbuffer", bfbs, {}, message);
    NX_CHECK(bfbs_view.has_value());
    if (bfbs_view.has_value()) {
        NX_CHECK(bfbs_view->kind == PayloadKind::Opaque);
    }
    NX_CHECK(
        !nexigen::recorder::decode_payload("flatbuffer", bad_descriptor, {}, message).has_value());

    // Unknown encodings report a named error.
    const auto unknown = nexigen::recorder::decode_payload("capnproto", {}, {}, message);
    NX_CHECK(!unknown.has_value());
    if (!unknown.has_value()) {
        NX_CHECK(unknown.error() == nexigen::core::ErrorCode::UnsupportedFormat);
    }
    NX_CHECK(nexigen::recorder::encoding_supported("protobuf"));
    NX_CHECK(!nexigen::recorder::encoding_supported("capnproto"));
}

void test_payload_decoder_extended() {
    using nexigen::recorder::DatasetField;
    using nexigen::recorder::PayloadKind;

    auto field = [](const char* name, const char* type, std::uint32_t offset, std::uint32_t size,
                    std::uint32_t count, std::uint32_t stride) {
        DatasetField out{};
        out.name = name;
        out.type = type;
        out.offset = offset;
        out.element_size = size;
        out.count = count;
        out.stride = stride;
        return out;
    };

    // Signed, float, and array fields exercise every scalar branch.
    std::array<std::byte, 40> pod{};
    auto put = [&pod](std::size_t offset, std::uint64_t value, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            pod[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
        }
    };
    put(0U, 0xFFU, 1U);
    put(1U, 0xFFFEU, 2U);
    put(3U, 0xFFFFFFFDU, 4U);
    put(7U, 0xFFFFFFFFFFFFFFFCULL, 8U);
    const float f32 = 1.5F;
    std::memcpy(&pod[15], &f32, sizeof(f32));
    const double f64 = 2.5;
    std::memcpy(&pod[19], &f64, sizeof(f64));
    put(27U, 1U, 2U);
    put(29U, 2U, 2U);
    put(31U, 3U, 2U);
    const std::vector<DatasetField> fields{
        field("i8", "i8", 0U, 1U, 1U, 1U),    field("i16", "i16", 1U, 2U, 1U, 2U),
        field("i32", "i32", 3U, 4U, 1U, 4U),  field("i64", "i64", 7U, 8U, 1U, 8U),
        field("f32", "f32", 15U, 4U, 1U, 4U), field("f64", "f64", 19U, 8U, 1U, 8U),
        field("arr", "u16", 27U, 2U, 3U, 2U)};
    const auto decoded = nexigen::recorder::decode_payload("jsonschema", {}, fields, pod);
    NX_CHECK(decoded.has_value());
    if (decoded.has_value()) {
        const auto document = nlohmann::json::parse(decoded->text, nullptr, false);
        NX_CHECK(!document.is_discarded());
        NX_CHECK(document["i8"] == -1);
        NX_CHECK(document["i16"] == -2);
        NX_CHECK(document["i32"] == -3);
        NX_CHECK(document["i64"] == -4);
        NX_CHECK(document["f32"] == 1.5);
        NX_CHECK(document["f64"] == 2.5);
        NX_CHECK(document["arr"].size() == 3U);
        NX_CHECK(document["arr"][2] == 3);
    }

    // An unsupported scalar type and a truncated payload are rejected.
    const auto unknown_type = nexigen::recorder::decode_payload(
        "jsonschema", {}, std::vector<DatasetField>{field("v", "u128", 0U, 16U, 1U, 16U)}, pod);
    NX_CHECK(!unknown_type.has_value());
    if (!unknown_type.has_value()) {
        NX_CHECK(unknown_type.error() == nexigen::core::ErrorCode::UnsupportedFormat);
    }
    const auto truncated = nexigen::recorder::decode_payload(
        "jsonschema", {}, std::vector<DatasetField>{field("v", "u32", 8U, 4U, 1U, 4U)},
        std::span<const std::byte>(pod).first(4));
    NX_CHECK(!truncated.has_value());
    if (!truncated.has_value()) {
        NX_CHECK(truncated.error() == nexigen::core::ErrorCode::CorruptData);
    }

    // JSON documents that do not parse are corrupt.
    const std::string bad_json = "not-json";
    const auto bad_json_span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(bad_json.data()), bad_json.size());
    const auto bad_json_view = nexigen::recorder::decode_payload("json", {}, {}, bad_json_span);
    NX_CHECK(!bad_json_view.has_value());
    if (!bad_json_view.has_value()) {
        NX_CHECK(bad_json_view.error() == nexigen::core::ErrorCode::CorruptData);
    }

    // Opaque payloads cover every base64 padding branch.
    const std::array<std::uint8_t, 1> ros_descriptor{0x01U};
    for (std::size_t size = 1U; size <= 3U; ++size) {
        std::array<std::byte, 3> bytes{};
        for (std::size_t i = 0; i < size; ++i) {
            bytes[i] = static_cast<std::byte>(0x40U + i);
        }
        const auto view = nexigen::recorder::decode_payload(
            "ros2msg", ros_descriptor, {}, std::span<const std::byte>(bytes).first(size));
        NX_CHECK(view.has_value());
        if (view.has_value()) {
            NX_CHECK(view->kind == PayloadKind::Opaque);
            NX_CHECK(view->text.size() == 4U);
            if (size == 1U) {
                NX_CHECK(view->text.ends_with("=="));
            } else if (size == 2U) {
                NX_CHECK(view->text.ends_with("="));
            } else {
                NX_CHECK(!view->text.ends_with("="));
            }
        }
    }

    // Protobuf wire types 0, 1, 2 and 5, including a nested message.
    const std::array<std::uint8_t, 14> descriptor{0x0AU, 0x0CU, 0x0AU, 0x0AU, 't', 'e', 's',
                                                  't',   '.',   'p',   'r',   'o', 't', 'o'};
    std::vector<std::byte> proto{std::byte{0x08}, std::byte{0x2A}, // varint
                                 std::byte{0x11}, std::byte{1},    std::byte{2},
                                 std::byte{3}, // fixed64
                                 std::byte{4},    std::byte{5},    std::byte{6},
                                 std::byte{7},    std::byte{8},    std::byte{0x1D},
                                 std::byte{9},    std::byte{10},   std::byte{11}, // fixed32
                                 std::byte{12},   std::byte{0x22}, std::byte{0x02},
                                 std::byte{0x08}, std::byte{0x01},  // nested
                                 std::byte{0x2A}, std::byte{0x00}}; // empty nested
    const auto proto_view = nexigen::recorder::decode_payload("protobuf", descriptor, {}, proto);
    NX_CHECK(proto_view.has_value());
    if (proto_view.has_value()) {
        NX_CHECK(proto_view->kind == PayloadKind::Opaque);
    }

    // Missing descriptors are named errors per encoding.
    const auto proto_empty = nexigen::recorder::decode_payload("protobuf", {}, {}, proto);
    NX_CHECK(!proto_empty.has_value());
    if (!proto_empty.has_value()) {
        NX_CHECK(proto_empty.error() == nexigen::core::ErrorCode::CorruptData);
    }
    const auto flat_empty = nexigen::recorder::decode_payload("flatbuffer", {}, {}, proto);
    NX_CHECK(!flat_empty.has_value());
    if (!flat_empty.has_value()) {
        NX_CHECK(flat_empty.error() == nexigen::core::ErrorCode::CorruptData);
    }
    const auto ros_empty = nexigen::recorder::decode_payload("ros2msg", {}, {}, proto);
    NX_CHECK(!ros_empty.has_value());
    if (!ros_empty.has_value()) {
        NX_CHECK(ros_empty.error() == nexigen::core::ErrorCode::NotFound);
    }
}

void test_opaque_channel_decode() {
    const std::string root = temp_root() + "-opaque";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U << 20U;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 4096;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "sensor.Opaque";
    schema.version = 1;
    schema.encoding = "protobuf";
    const std::array<std::uint8_t, 14> descriptor{0x0AU, 0x0CU, 0x0AU, 0x0AU, 't', 'e', 's',
                                                  't',   '.',   'p',   'r',   'o', 't', 'o'};
    schema.data.assign(descriptor.begin(), descriptor.end());
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "sensor/opaque", 1, nexigen::recorder::DatasetKind::Opaque, "protobuf");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::array<std::byte, 3> payload{std::byte{0x08}, std::byte{0x2A}, std::byte{0x00}};
    NX_CHECK(writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload, 1, 1, 1)
                 .has_value());
    NX_CHECK(writer->close().has_value());

    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    const auto datasets = reader->datasets();
    NX_CHECK(datasets.has_value() && datasets->size() == 1);
    if (!datasets.has_value() || datasets->empty()) {
        return;
    }
    const auto& dataset = datasets->front();
    NX_CHECK(dataset.schema_encoding == "protobuf");
    NX_CHECK(dataset.message_encoding == "protobuf");
    NX_CHECK(dataset.fields.empty());

    struct DecodeState {
        const nexigen::recorder::SessionReader* reader{nullptr};
        std::span<const std::uint8_t> descriptor{};
        bool ok{false};
    };
    DecodeState state{};
    state.reader = &*reader;
    if (!reader->manifest().schemas.empty()) {
        state.descriptor = reader->manifest().schemas.front().data;
    }
    const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& /*header*/,
                            std::span<const std::byte> record_payload, std::uint64_t /*offset*/) {
        auto& local = *static_cast<DecodeState*>(context);
        const auto view =
            nexigen::recorder::decode_payload("protobuf", local.descriptor, {}, record_payload);
        local.ok = view.has_value() && view->kind == nexigen::recorder::PayloadKind::Opaque &&
                   !view->text.empty();
        return local.ok;
    };
    NX_CHECK(reader->for_each(dataset.channel_id, true, 0, 0, false, visitor, &state).has_value());
    NX_CHECK(state.ok);
    if (std::getenv("NEXIGEN_RECORDER_KEEP") == nullptr) {
        std::filesystem::remove_all(root, error);
    }
}

void test_time_sync_record() {
    const std::string root = temp_root() + "-timesync";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U << 20U;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 4096;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    const std::int64_t offset = 1'700'000'000'000'000'000LL;
    NX_CHECK(writer->append_time_sync(offset).has_value());
    NX_CHECK(writer->close().has_value());

    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (reader.has_value()) {
        struct SyncState {
            bool found{false};
            std::int64_t offset{0};
        };
        SyncState state{};
        const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& header,
                                std::span<const std::byte> payload, std::uint64_t /*offset*/) {
            if (header.type !=
                    static_cast<std::uint16_t>(nexigen::recorder::RecordType::TimeSync) ||
                payload.size() != sizeof(nexigen::recorder::TimeSyncPayload)) {
                return true;
            }
            auto& local = *static_cast<SyncState*>(context);
            nexigen::recorder::TimeSyncPayload sync{};
            std::memcpy(&sync, payload.data(), sizeof(sync));
            local.offset = sync.utc_offset_ns;
            local.found = true;
            return true;
        };
        NX_CHECK(reader->for_each(0, false, 0, 0, false, visitor, &state).has_value());
        NX_CHECK(state.found);
        NX_CHECK(state.offset == offset);
    }
    if (std::getenv("NEXIGEN_RECORDER_KEEP") == nullptr) {
        std::filesystem::remove_all(root, error);
    }
}

void test_publisher_epochs() {
    const std::string root = temp_root() + "-epochs";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U << 20U;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 4096;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Epoch";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/epoch", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::vector<std::byte> payload(16);
    for (std::uint64_t sequence = 0; sequence < 3; ++sequence) {
        NX_CHECK(writer
                     ->append(nexigen::recorder::RecordType::ComPayload, *channel, payload,
                              sequence, sequence, 7)
                     .has_value());
    }
    for (std::uint64_t sequence = 0; sequence < 2; ++sequence) {
        NX_CHECK(writer
                     ->append(nexigen::recorder::RecordType::ComPayload, *channel, payload,
                              sequence + 3, sequence, 8)
                     .has_value());
    }
    // A backwards sequence on the same publisher id also starts a new epoch.
    NX_CHECK(
        writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload, 10U, 0U, 8U)
            .has_value());
    NX_CHECK(writer->close().has_value());
    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (reader.has_value()) {
        const auto epochs = reader->publisher_epochs();
        NX_CHECK(epochs.has_value() && epochs->size() == 3);
        if (epochs.has_value() && epochs->size() == 3) {
            NX_CHECK((*epochs)[0].publisher_id == 7);
            NX_CHECK((*epochs)[0].first_sequence == 0);
            NX_CHECK((*epochs)[0].last_sequence == 2);
            NX_CHECK((*epochs)[0].records == 3);
            NX_CHECK((*epochs)[1].publisher_id == 8);
            NX_CHECK((*epochs)[1].records == 2);
            // Backwards sequence: same publisher, new epoch.
            NX_CHECK((*epochs)[2].publisher_id == 8);
            NX_CHECK((*epochs)[2].records == 1);
        }
    }
    std::filesystem::remove_all(root, error);
}

void test_segment_only_open() {
    const std::string root = temp_root() + "-segment";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U << 20U;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 4096;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Segment";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/segment", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::vector<std::byte> payload(32);
    for (std::uint64_t i = 0; i < 3; ++i) {
        NX_CHECK(
            writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload, i, i, 1)
                .has_value());
    }
    NX_CHECK(writer->close().has_value());
    NX_CHECK(!writer->manifest().segments.empty());
    const std::string source = root + "/sess/" + writer->manifest().segments.front().rec_path;
    NX_CHECK(!source.empty());

    const std::string isolated = temp_root() + "-segment-only";
    std::filesystem::remove_all(isolated, error);
    std::filesystem::create_directories(isolated, error);
    const std::string copied = isolated + "/segment_000000.nxrec";
    std::filesystem::copy_file(source, copied, std::filesystem::copy_options::overwrite_existing,
                               error);
    NX_CHECK(!error);

    auto reader = nexigen::recorder::SessionReader::open_segment(copied);
    NX_CHECK(reader.has_value());
    if (reader.has_value()) {
        const auto datasets = reader->datasets();
        NX_CHECK(datasets.has_value() && datasets->size() == 1);
        std::uint64_t visited = 0;
        const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& /*header*/,
                                std::span<const std::byte> /*payload*/, std::uint64_t /*offset*/) {
            ++static_cast<std::uint64_t*>(context)[0];
            return true;
        };
        NX_CHECK(reader->for_each(*channel, true, 0, 0, false, visitor, &visited).has_value());
        NX_CHECK(visited == 3);
        // No `.nxidx` exists next to the copied segment; a ranged query rebuilds it.
        std::uint64_t ranged = 0;
        // Recovered bounds use the commit time for records without a documented
        // event field, so a full widening window is the portable check.
        NX_CHECK(
            reader->for_each(*channel, true, 0, UINT64_MAX, true, visitor, &ranged).has_value());
        NX_CHECK(ranged == 3);
    }
    std::filesystem::remove_all(root, error);
    std::filesystem::remove_all(isolated, error);
}

void test_shard_overflow_gap() {
    const std::string root = temp_root() + "-shard-gap";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U << 24U;
    settings.write_buffer_bytes = 1U << 20U;
    settings.index_buffer_bytes = 64U * 1024U;
    settings.max_record_bytes = 4096;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.ShardGap";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/shard-gap", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    nexigen::recorder::RecordShard shard{};
    for (std::uint64_t i = 0; i < nexigen::recorder::RecordShard::capacity(); ++i) {
        nexigen::recorder::RecordEnvelope envelope{};
        envelope.type = nexigen::recorder::RecordType::ComPayload;
        envelope.channel_id = *channel;
        envelope.payload_size = 32;
        envelope.event_ns = i;
        envelope.sequence = i;
        envelope.publisher_id = 1;
        NX_CHECK(shard.try_push(envelope).has_value());
    }
    nexigen::recorder::RecordEnvelope overflow{};
    overflow.type = nexigen::recorder::RecordType::ComPayload;
    overflow.channel_id = *channel;
    overflow.payload_size = 32;
    overflow.event_ns = nexigen::recorder::RecordShard::capacity();
    overflow.sequence = nexigen::recorder::RecordShard::capacity();
    overflow.publisher_id = 1;
    NX_CHECK(!shard.try_push(overflow).has_value());
    NX_CHECK(writer->append_from_shard(shard).has_value());
    NX_CHECK(writer->close().has_value());

    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (reader.has_value()) {
        std::uint64_t records = 0;
        std::uint64_t gaps = 0;
        NX_CHECK(reader->verify(records, gaps).has_value());
        NX_CHECK(gaps == 1);
        struct GapState {
            std::uint64_t first{0};
            std::uint64_t lost{0};
            std::uint16_t reason{0};
            bool found{false};
        };
        GapState state{};
        const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& header,
                                std::span<const std::byte> payload, std::uint64_t /*offset*/) {
            if (header.type !=
                static_cast<std::uint16_t>(nexigen::recorder::RecordType::GapMarker)) {
                return true;
            }
            auto& local = *static_cast<GapState*>(context);
            nexigen::recorder::GapMarkerPayload gap{};
            std::memcpy(&gap, payload.data(), sizeof(gap));
            local.first = gap.first_lost_sequence;
            local.lost = gap.lost_count;
            local.reason = gap.reason;
            local.found = true;
            return true;
        };
        NX_CHECK(reader->for_each(0, false, 0, 0, false, visitor, &state).has_value());
        NX_CHECK(state.found);
        NX_CHECK(state.first == nexigen::recorder::RecordShard::capacity());
        NX_CHECK(state.lost == 1);
        NX_CHECK(state.reason ==
                 static_cast<std::uint16_t>(nexigen::recorder::GapReason::IoBackpressure));
    }
    std::filesystem::remove_all(root, error);
}

void test_time_base_history() {
    using nexigen::recorder::ChannelDeclare;
    using nexigen::recorder::TimeBasePayload;
    using nexigen::recorder::TimeBaseResolver;

    // Layout and serialization round-trip.
    static_assert(sizeof(TimeBasePayload) == 4224U);
    static_assert(alignof(TimeBasePayload) == 64U);
    static_assert(offsetof(TimeBasePayload, rows) == 128U);
    static_assert(sizeof(nexigen::chrono::TimeSnapshot) == 896U);

    constexpr std::uint64_t kRealtimeBase = 1'700'000'000'000'000'000ULL;
    constexpr std::uint64_t kTaiOffset = 37'000'000'000ULL;

    TimeBasePayload entry{};
    entry.generation = 7;
    entry.time_base_epoch = 42;
    entry.boot_token = 0x0123456789abcdefULL;
    entry.status = nexigen::chrono::TimeBaseStatus::Ready;
    entry.sync_mode = nexigen::chrono::SyncMode::PtpSynced;
    entry.service_flags = nexigen::chrono::ServiceFlag::PhcPresent |
                          nexigen::chrono::ServiceFlag::TaiSynchronized |
                          nexigen::chrono::ServiceFlag::DeclaredDomainsComplete;
    entry.tai_offset_ns = static_cast<std::int64_t>(kTaiOffset);
    entry.utc_offset_ns = 0;
    entry.utc_unc_ns = 250;
    entry.domain_count = 3;
    NX_CHECK(entry.has_current_layout());
    std::memcpy(entry.boot_uuid.data(), "01234567-89ab-cdef-0123-456789abcdef", 36);

    entry.rows[0].domain_id = 7;
    entry.rows[0].domain_class = nexigen::chrono::TimeDomainClass::DeviceClock;
    entry.rows[0].lock = nexigen::chrono::LockState::Locked;
    entry.rows[0].epoch = 5;
    entry.rows[0].native_anchor = 1'000U;
    entry.rows[0].canonical_anchor_ns = 2'000;
    entry.rows[0].scale_fwd_q32 = 1ULL << 32U;
    entry.rows[0].scale_rev_q32 = 1ULL << 32U;
    entry.rows[0].unc_ns = 40;
    entry.rows[0].valid_from_native = 0U;
    entry.rows[0].valid_to_native = 10'000U;
    entry.rows[0].valid_from_canonical = 1'000U;
    entry.rows[0].valid_to_canonical = 11'000U;
    entry.rows[0].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;

    entry.rows[1].domain_id = nexigen::chrono::k_realtime_domain_id;
    entry.rows[1].domain_class = nexigen::chrono::TimeDomainClass::SystemClock;
    entry.rows[1].lock = nexigen::chrono::LockState::Locked;
    entry.rows[1].epoch = 1;
    entry.rows[1].native_anchor = kRealtimeBase;
    entry.rows[1].canonical_anchor_ns = 2'000;
    entry.rows[1].scale_fwd_q32 = 1ULL << 32U;
    entry.rows[1].scale_rev_q32 = 1ULL << 32U;
    entry.rows[1].valid_from_canonical = 1'000U;
    entry.rows[1].valid_to_canonical = 11'000U;
    entry.rows[1].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;

    entry.rows[2].domain_id = nexigen::chrono::k_tai_domain_id;
    entry.rows[2].domain_class = nexigen::chrono::TimeDomainClass::SystemClock;
    entry.rows[2].lock = nexigen::chrono::LockState::Locked;
    entry.rows[2].epoch = 1;
    entry.rows[2].native_anchor = kRealtimeBase + kTaiOffset;
    entry.rows[2].canonical_anchor_ns = 2'000;
    entry.rows[2].scale_fwd_q32 = 1ULL << 32U;
    entry.rows[2].scale_rev_q32 = 1ULL << 32U;
    entry.rows[2].valid_from_canonical = 1'000U;
    entry.rows[2].valid_to_canonical = 11'000U;
    entry.rows[2].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;

    nlohmann::json document = entry;
    TimeBasePayload decoded{};
    nexigen::recorder::from_json(document, decoded);
    NX_CHECK(decoded.generation == entry.generation);
    NX_CHECK(decoded.time_base_epoch == entry.time_base_epoch);
    NX_CHECK(decoded.boot_token == entry.boot_token);
    NX_CHECK(decoded.boot_uuid_view() == "01234567-89ab-cdef-0123-456789abcdef");
    NX_CHECK(decoded.domain_count == 3U);
    NX_CHECK(decoded.status == nexigen::chrono::TimeBaseStatus::Ready);
    NX_CHECK(decoded.sync_mode == nexigen::chrono::SyncMode::PtpSynced);
    NX_CHECK(decoded.service_flags.has(nexigen::chrono::ServiceFlag::PhcPresent));
    NX_CHECK(decoded.service_flags.has(nexigen::chrono::ServiceFlag::TaiSynchronized));
    NX_CHECK(decoded.service_flags.has(nexigen::chrono::ServiceFlag::DeclaredDomainsComplete));
    NX_CHECK(!decoded.service_flags.has(nexigen::chrono::ServiceFlag::StepDetected));
    NX_CHECK(decoded.tai_offset_ns == entry.tai_offset_ns);
    NX_CHECK(decoded.utc_unc_ns == entry.utc_unc_ns);
    NX_CHECK(decoded.rows[0].domain_id == 7U);
    NX_CHECK(decoded.rows[0].epoch == 5U);
    NX_CHECK(decoded.rows[2].domain_id == nexigen::chrono::k_tai_domain_id);
    NX_CHECK(decoded.has_current_layout());
    NX_CHECK(!entry.to_string().empty());

    // Resolution: native -> hub -> any domain, and explicit gaps.
    TimeBaseResolver resolver{};
    NX_CHECK(resolver.empty());
    NX_CHECK(!resolver.canonical_from_native(7, 5, 1'500U).has_value());
    NX_CHECK(resolver.add(entry));
    NX_CHECK(resolver.boot_token() == std::optional<std::uint64_t>{0x0123456789abcdefULL});
    NX_CHECK(resolver.time_base_epoch() == std::optional<std::uint64_t>{42U});
    const auto canonical = resolver.canonical_from_native(7, 5, 1'500U);
    NX_CHECK(canonical == std::optional<std::int64_t>{2'500});
    NX_CHECK(!resolver.canonical_from_native(7, 6, 1'500U).has_value());  // wrong epoch
    NX_CHECK(!resolver.canonical_from_native(8, 5, 1'500U).has_value());  // unknown domain
    NX_CHECK(!resolver.canonical_from_native(7, 5, 20'000U).has_value()); // outside validity
    NX_CHECK(
        resolver.canonical_from_native(nexigen::chrono::k_monotonic_raw_domain_id, 0, 1'234U) ==
        std::optional<std::int64_t>{1'234}); // hub resolves to itself
    NX_CHECK(resolver.native_from_canonical(7, 5, 2'500) == std::optional<std::uint64_t>{1'500U});
    NX_CHECK(resolver.convert(7, 5, 1'500U, nexigen::chrono::k_realtime_domain_id) ==
             std::optional<std::uint64_t>{kRealtimeBase + 500U});
    NX_CHECK(resolver.convert(7, 5, 1'500U, nexigen::chrono::k_tai_domain_id) ==
             std::optional<std::uint64_t>{kRealtimeBase + kTaiOffset + 500U});
    NX_CHECK(resolver.convert(nexigen::chrono::k_realtime_domain_id, 0, kRealtimeBase + 500U, 7) ==
             std::optional<std::uint64_t>{1'500U});
    NX_CHECK(!resolver.convert(7, 6, 1'500U, nexigen::chrono::k_realtime_domain_id).has_value());

    const auto utc = resolver.utc_from_canonical(2'500U);
    NX_CHECK(utc == std::optional<std::int64_t>{static_cast<std::int64_t>(kRealtimeBase + 500U)});
    NX_CHECK(
        resolver.tai_from_canonical(2'500U) ==
        std::optional<std::int64_t>{static_cast<std::int64_t>(kRealtimeBase + kTaiOffset + 500U)});
    NX_CHECK(!resolver.utc_from_canonical(500U).has_value());    // before the window
    NX_CHECK(!resolver.utc_from_canonical(12'000U).has_value()); // after the window

    // A gap entry is never substituted for a map, even when it carries rows.
    TimeBasePayload gap{};
    gap.generation = 8;
    gap.time_base_epoch = 42;
    gap.boot_token = entry.boot_token;
    gap.gap = 1U;
    gap.domain_count = 1U;
    gap.rows[0] = entry.rows[1];
    gap.rows[0].native_anchor = 999U;
    NX_CHECK(resolver.add(gap));
    NX_CHECK(utc == resolver.utc_from_canonical(2'500U)); // gap does not override
    NX_CHECK(resolver.utc_from_canonical(7'000U) ==
             std::optional<std::int64_t>{static_cast<std::int64_t>(kRealtimeBase + 5'000U)});
    TimeBaseResolver gap_only{};
    NX_CHECK(gap_only.add(gap));
    NX_CHECK(!gap_only.utc_from_canonical(7'000U).has_value()); // only the gap covers it

    // A recorded snapshot anchors the row active at its generation.
    TimeBasePayload narrow = entry;
    narrow.generation = 7U;
    narrow.domain_count = 1U;
    narrow.rows[0].valid_to_native = 3'000U;
    nexigen::chrono::TimeSnapshot snapshot{};
    snapshot.generation = 7U;
    snapshot.boot_token = entry.boot_token;
    snapshot.status = nexigen::chrono::TimeBaseStatus::Ready;
    snapshot.sync_mode = nexigen::chrono::SyncMode::PtpSynced;
    snapshot.monotonic_raw_ns = 5'000U;
    snapshot.domain_count = 1U;
    snapshot.domains[0].domain_id = 7U;
    snapshot.domains[0].domain_class = nexigen::chrono::TimeDomainClass::DeviceClock;
    snapshot.domains[0].lock = nexigen::chrono::LockState::Locked;
    snapshot.domains[0].epoch = 5U;
    snapshot.domains[0].value = 4'000U;
    NX_CHECK(narrow.has_current_layout());

    TimeBaseResolver snapshots{};
    NX_CHECK(snapshots.add(narrow));
    NX_CHECK(!snapshots.canonical_from_native(7, 5, 4'400U).has_value()); // row is too narrow
    snapshots.add_snapshot(snapshot);
    NX_CHECK(snapshots.snapshot_count() == 1U);
    NX_CHECK(snapshots.canonical_from_native(7, 5, 4'400U) ==
             std::optional<std::int64_t>{5'400});                         // 5000 + (4400 - 4000)
    NX_CHECK(!snapshots.canonical_from_native(7, 9, 4'400U).has_value()); // snapshot epoch differs
    nexigen::chrono::TimeSnapshot other_boot = snapshot;
    other_boot.boot_token = 0xfeedfacecafebeefULL;
    TimeBaseResolver wrong_boot{};
    NX_CHECK(wrong_boot.add(narrow));
    wrong_boot.add_snapshot(other_boot);
    NX_CHECK(!wrong_boot.canonical_from_native(7, 5, 4'400U).has_value());

    // The retired payload layout is rejected, not reinterpreted.
    TimeBasePayload legacy = entry;
    legacy.layout = 0U;
    TimeBaseResolver reject{};
    NX_CHECK(!reject.add(legacy));
    NX_CHECK(reject.saw_legacy_payload());
    NX_CHECK(reject.size() == 0U);
    reject.clear();
    NX_CHECK(!reject.saw_legacy_payload());
    NX_CHECK(reject.empty());

    // Channel native-domain declaration round-trips and validates.
    ChannelDeclare channel{};
    channel.id = 3;
    channel.schema_id = 8;
    channel.topic = "fpga/events";
    channel.message_encoding = "nexigen.pod";
    NX_CHECK(channel.native_domain == "monotonic_raw"); // hub default
    channel.native_domain = "device";
    channel.domain_id = 7;
    channel.domain_epoch = 5;
    channel.domain_class = nexigen::chrono::TimeDomainClass::DeviceClock;
    channel.access_mode = nexigen::chrono::DeviceAccessMode::PhcCrossTimestamp;
    NX_CHECK(channel.validate().has_value());
    const nlohmann::json channel_json = channel;
    ChannelDeclare channel_back{};
    nexigen::recorder::from_json(channel_json, channel_back);
    NX_CHECK(channel_back.native_domain == "device");
    NX_CHECK(channel_back.domain_id == 7U);
    NX_CHECK(channel_back.domain_epoch == 5U);
    NX_CHECK(channel_back.domain_class == nexigen::chrono::TimeDomainClass::DeviceClock);
    NX_CHECK(channel_back.access_mode == nexigen::chrono::DeviceAccessMode::PhcCrossTimestamp);
    ChannelDeclare host{};
    host.id = 4;
    host.schema_id = 8;
    host.topic = "host/counter";
    host.message_encoding = "nexigen.pod";
    host.native_domain = "host_counter";
    host.domain_id = nexigen::chrono::k_host_counter_domain_id;
    NX_CHECK(host.validate().has_value());
    ChannelDeclare unknown = channel;
    unknown.native_domain = "quantum";
    NX_CHECK(!unknown.validate().has_value());

    // A session from the retired payload contract is rejected wholesale.
    nexigen::recorder::SessionManifest legacy_manifest{};
    legacy_manifest.session_id = "legacy";
    legacy_manifest.version = 1;
    const auto legacy_status = legacy_manifest.validate();
    NX_CHECK(!legacy_status.has_value());
    NX_CHECK(legacy_status.error() == nexigen::core::ErrorCode::UnsupportedFormat);
    NX_CHECK(nexigen::recorder::SessionManifest{}.validate().has_value() ||
             nexigen::recorder::SessionManifest{}.version == nexigen::recorder::kFormatVersion);

    // Manifest binding: boot token, UUID and time-base epoch survive.
    const std::string root = temp_root() + "-timebase";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.write_buffer_bytes = 8192;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 8192;
    settings.boot_token = "0123456789abcdef";
    settings.boot_uuid = "01234567-89ab-cdef-0123-456789abcdef";
    settings.time_base_epoch = 42;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (writer.has_value()) {
        NX_CHECK(writer->append_time_base(entry).has_value());
        NX_CHECK(writer->append_time_snapshot(snapshot).has_value());
        NX_CHECK(writer->declare_schema(nexigen::recorder::SchemaDeclare{}).has_value() ==
                 false); // empty schema is rejected
        NX_CHECK(writer->close().has_value());
        auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
        NX_CHECK(reader.has_value());
        if (reader.has_value()) {
            const auto& manifest = reader->manifest();
            NX_CHECK(manifest.boot_token == "0123456789abcdef");
            NX_CHECK(manifest.boot_uuid == "01234567-89ab-cdef-0123-456789abcdef");
            NX_CHECK(manifest.time_base_epoch == 42U);
            struct State {
                bool base_found{false};
                bool snapshot_found{false};
                TimeBasePayload entry{};
                nexigen::chrono::TimeSnapshot snapshot{};
                std::uint32_t base_crc{0};
                std::uint32_t snapshot_crc{0};
            };
            State state{};
            const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& header,
                                    std::span<const std::byte> payload, std::uint64_t) {
                auto& local = *static_cast<State*>(context);
                const auto bytes = std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());
                if (header.type ==
                        static_cast<std::uint16_t>(nexigen::recorder::RecordType::TimeBase) &&
                    payload.size() == sizeof(TimeBasePayload)) {
                    std::memcpy(&local.entry, payload.data(), sizeof(local.entry));
                    local.base_found = true;
                    local.base_crc = header.payload_crc32c;
                    NX_CHECK(nexigen::core::integrity::crc32c(bytes) == header.payload_crc32c);
                }
                if (header.type ==
                        static_cast<std::uint16_t>(nexigen::recorder::RecordType::TimeSnapshot) &&
                    payload.size() == sizeof(nexigen::chrono::TimeSnapshot)) {
                    std::memcpy(&local.snapshot, payload.data(), sizeof(local.snapshot));
                    local.snapshot_found = true;
                    local.snapshot_crc = header.payload_crc32c;
                    NX_CHECK(nexigen::core::integrity::crc32c(bytes) == header.payload_crc32c);
                }
                return true;
            };
            NX_CHECK(reader->for_each(0, false, 0, 0, false, visitor, &state).has_value());
            NX_CHECK(state.base_found);
            NX_CHECK(state.snapshot_found);
            NX_CHECK(state.base_crc != 0U);
            NX_CHECK(state.snapshot_crc != 0U);
            NX_CHECK(state.entry.generation == 7U);
            NX_CHECK(state.entry.has_current_layout());
            NX_CHECK(state.entry.status == nexigen::chrono::TimeBaseStatus::Ready);
            NX_CHECK(state.entry.sync_mode == nexigen::chrono::SyncMode::PtpSynced);
            NX_CHECK(state.entry.rows[0].domain_id == 7U);
            NX_CHECK(state.snapshot.generation == 7U);
            NX_CHECK(state.snapshot.monotonic_raw_ns == 5'000U);
            NX_CHECK(state.snapshot.domain_count == 1U);
            NX_CHECK(state.snapshot.domains[0].value == 4'000U);
        }
    }
    std::filesystem::remove_all(root, error);
}

void test_time_base_session_bridge() {
    const std::string root = temp_root() + "-bridge";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "bridge";
    settings.write_buffer_bytes = 8192;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 8192;
    settings.boot_uuid = "01234567-89ab-cdef-0123-456789abcdef";
    settings.boot_token = "0123456789abcdef";
    settings.time_base_epoch = 9;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::TimeBaseSessionBridge bridge{*writer, "01234567-89ab-cdef-0123-456789abcdef",
                                                    9U};

    nexigen::chrono::TimeBaseBlock block{};
    block.generation = 2U;
    block.boot_token = 9U;
    block.status = nexigen::chrono::TimeBaseStatus::Ready;
    block.sync_mode = nexigen::chrono::SyncMode::PtpSynced;
    block.domain_count = 1U;
    block.rows[0].domain_id = nexigen::chrono::k_monotonic_raw_domain_id;
    block.rows[0].domain_class = nexigen::chrono::TimeDomainClass::SystemClock;
    block.rows[0].lock = nexigen::chrono::LockState::Locked;
    block.rows[0].scale_fwd_q32 = 1ULL << 32U;
    block.rows[0].scale_rev_q32 = 1ULL << 32U;
    block.rows[0].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;
    block.last_snapshot.generation = 1U;
    block.last_snapshot.sync_mode = nexigen::chrono::SyncMode::PtpSynced;

    NX_CHECK(bridge.record(block));
    NX_CHECK(bridge.recorded_entries() == 1U);
    NX_CHECK(bridge.recorded_snapshots() == 1U);
    // The same generation is recorded once.
    NX_CHECK(bridge.record(block));
    NX_CHECK(bridge.recorded_entries() == 1U);
    NX_CHECK(bridge.recorded_snapshots() == 1U);

    block.generation = 4U;
    block.last_snapshot.generation = 2U;
    NX_CHECK(bridge.record(block));
    NX_CHECK(bridge.recorded_entries() == 2U);
    NX_CHECK(bridge.recorded_snapshots() == 2U);

    // A lost base becomes an explicit gap entry with no rows.
    block.generation = 6U;
    block.status = nexigen::chrono::TimeBaseStatus::Lost;
    NX_CHECK(bridge.record(block));
    NX_CHECK(bridge.recorded_entries() == 3U);

    NX_CHECK(writer->close().has_value());
    // A closed writer makes the bridge report failure instead of throwing.
    block.generation = 8U;
    NX_CHECK(!bridge.record(block));

    auto reader = nexigen::recorder::SessionReader::open(root + "/bridge");
    NX_CHECK(reader.has_value());
    if (reader.has_value()) {
        struct Counts {
            std::uint32_t time_base{0};
            std::uint32_t snapshots{0};
            std::uint32_t gaps{0};
        } counts{};
        const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& header,
                                std::span<const std::byte> payload, std::uint64_t /*offset*/) {
            auto& state = *static_cast<Counts*>(context);
            if (header.type ==
                static_cast<std::uint16_t>(nexigen::recorder::RecordType::TimeBase)) {
                ++state.time_base;
                if (payload.size() == sizeof(nexigen::recorder::TimeBasePayload)) {
                    nexigen::recorder::TimeBasePayload entry{};
                    std::memcpy(&entry, payload.data(), sizeof(entry));
                    state.gaps += entry.gap;
                }
            } else if (header.type ==
                       static_cast<std::uint16_t>(nexigen::recorder::RecordType::TimeSnapshot)) {
                ++state.snapshots;
            }
            return true;
        };
        NX_CHECK(reader->for_each(0U, false, 0U, 0U, false, visitor, &counts).has_value());
        NX_CHECK(counts.time_base == 3U);
        NX_CHECK(counts.snapshots == 2U);
        NX_CHECK(counts.gaps == 1U);
    }
    std::filesystem::remove_all(root, error);
}

void check_wire_struct_serialization() {
    using namespace nexigen::recorder;

    using namespace nexigen::recorder;

    check_recorder_enum_vocabulary<RecorderFormat>();
    check_recorder_enum_vocabulary<TimeDomain>();
    check_recorder_enum_vocabulary<RecordType>();
    check_recorder_enum_vocabulary<GapReason>();
    check_recorder_enum_vocabulary<VideoCodec>();
    check_recorder_enum_vocabulary<BitstreamFormat>();
    check_recorder_enum_vocabulary<VideoCrcMode>();
    check_recorder_enum_vocabulary<DatasetKind>();
    check_recorder_enum_vocabulary<RetentionPolicy>();
    check_recorder_enum_vocabulary<ExportFormat>();
    check_recorder_enum_vocabulary<VideoExportFormat>();

    RecordHeader header{};
    header.magic = kRecordMagic;
    header.type = static_cast<std::uint16_t>(RecordType::ComPayload);
    header.flags = 3U;
    header.channel_id = 7U;
    header.payload_size = 11U;
    header.commit_mono_ns = 13U;
    header.sequence = 17U;
    header.publisher_id = 19U;
    header.header_crc32c = 23U;
    header.payload_crc32c = 29U;
    NX_CHECK(!header.to_string().empty());
    NX_CHECK(std::format("{}", header) == header.to_string());
    {
        const nlohmann::json j = header;
        RecordHeader back{};
        from_json(j, back);
        NX_CHECK(back.magic == header.magic && back.type == header.type);
        NX_CHECK(back.channel_id == header.channel_id);
        NX_CHECK(back.payload_size == header.payload_size);
        NX_CHECK(back.commit_mono_ns == header.commit_mono_ns);
        NX_CHECK(back.sequence == header.sequence && back.publisher_id == header.publisher_id);
        NX_CHECK(back.header_crc32c == header.header_crc32c);
        NX_CHECK(back.payload_crc32c == header.payload_crc32c);
    }

    IndexEntry index{};
    index.commit_ns = 1U;
    index.rec_offset = 2U;
    index.min_event_ns = 3U;
    index.max_event_ns = 4U;
    index.channel_mask = 5U;
    index.end_rec_offset = 6U;
    index.record_count = 7U;
    index.flags = 8U;
    index.entry_crc32c = 9U;
    NX_CHECK(!index.to_string().empty());
    {
        const nlohmann::json j = index;
        IndexEntry back{};
        from_json(j, back);
        NX_CHECK(back.commit_ns == 1U && back.rec_offset == 2U && back.record_count == 7U);
        NX_CHECK(back.entry_crc32c == 9U);
    }

    VideoFrameRef frame{};
    frame.vid_segment_seq = 2U;
    frame.vid_offset = 4096U;
    frame.vid_size = 1024U;
    frame.vid_crc32c = 0xABCDU;
    frame.codec = static_cast<std::uint16_t>(VideoCodec::H265);
    frame.bitstream = static_cast<std::uint16_t>(BitstreamFormat::AnnexB);
    frame.width = 1920U;
    frame.height = 1080U;
    frame.capture_mono_ns = 99U;
    frame.source_sequence = 5U;
    frame.config_epoch = 3U;
    NX_CHECK(!frame.to_string().empty());
    {
        const nlohmann::json j = frame;
        VideoFrameRef back{};
        from_json(j, back);
        NX_CHECK(back.vid_offset == 4096U && back.vid_size == 1024U);
        NX_CHECK(back.codec == static_cast<std::uint16_t>(VideoCodec::H265));
        NX_CHECK(back.bitstream == static_cast<std::uint16_t>(BitstreamFormat::AnnexB));
        NX_CHECK(back.width == 1920U && back.capture_mono_ns == 99U);
    }

    VideoStreamConfig config{};
    config.codec = static_cast<std::uint16_t>(VideoCodec::H264);
    config.bitstream = static_cast<std::uint16_t>(BitstreamFormat::Avcc);
    config.width = 1280U;
    config.height = 720U;
    config.timebase_num = 1U;
    config.timebase_den = 30U;
    config.extradata_size = 3U;
    config.config_epoch = 4U;
    config.extradata[0] = 0xAAU;
    config.extradata[1] = 0xBBU;
    config.extradata[2] = 0xCCU;
    NX_CHECK(!config.to_string().empty());
    {
        const nlohmann::json j = config;
        VideoStreamConfig back{};
        from_json(j, back);
        NX_CHECK(back.width == 1280U && back.timebase_den == 30U);
        NX_CHECK(back.extradata[0] == 0xAAU && back.extradata[2] == 0xCCU);
    }

    GapMarkerPayload gap{};
    gap.first_lost_sequence = 11U;
    gap.lost_count = 3U;
    gap.reason = static_cast<std::uint16_t>(GapReason::IoBackpressure);
    NX_CHECK(!gap.to_string().empty());
    {
        const nlohmann::json j = gap;
        GapMarkerPayload back{};
        from_json(j, back);
        NX_CHECK(back.first_lost_sequence == 11U && back.lost_count == 3U);
        NX_CHECK(back.reason == static_cast<std::uint16_t>(GapReason::IoBackpressure));
    }

    TimeSyncPayload sync{};
    sync.mono_ns = 5U;
    sync.utc_offset_ns = 100U;
    NX_CHECK(!sync.to_string().empty());
    {
        const nlohmann::json j = sync;
        TimeSyncPayload back{};
        from_json(j, back);
        NX_CHECK(back.mono_ns == 5U && back.utc_offset_ns == 100U);
    }

    SegmentFooter footer{};
    footer.magic = kFooterMagic;
    footer.version = kFormatVersion;
    footer.total_records = 10U;
    footer.total_gaps = 1U;
    footer.rec_bytes = 1000U;
    footer.vid_bytes = 2000U;
    footer.index_entries = 4U;
    footer.end_commit_ns = 42U;
    footer.footer_crc32c = 7U;
    NX_CHECK(!footer.to_string().empty());
    {
        const nlohmann::json j = footer;
        SegmentFooter back{};
        from_json(j, back);
        NX_CHECK(back.magic == kFooterMagic && back.total_records == 10U);
        NX_CHECK(back.rec_bytes == 1000U && back.footer_crc32c == 7U);
    }
}

void check_declaration_and_manifest_serialization() {
    using namespace nexigen::recorder;
    SchemaDeclare schema{};
    schema.id = 5U;
    schema.name = "test.Schema";
    schema.version = 2U;
    schema.encoding = "jsonschema";
    schema.units = "m/s";
    schema.endianness = "big";
    schema.data = {1U, 2U, 3U};
    schema.data_crc32c = 99U;
    NX_CHECK(!schema.to_string().empty());
    NX_CHECK(nexigen::recorder::to_string(schema) == schema.to_string());
    NX_CHECK(schema.validate().has_value());
    {
        const nlohmann::json j = schema;
        SchemaDeclare back{};
        from_json(j, back);
        NX_CHECK(back.id == 5U && back.name == "test.Schema");
        NX_CHECK(back.units == "m/s" && back.endianness == "big");
        NX_CHECK(back.data.size() == 3U && back.data_crc32c == 99U);
    }

    ChannelDeclare channel{};
    channel.id = 2U;
    channel.schema_id = 5U;
    channel.topic = "test/topic";
    channel.kind = DatasetKind::Video;
    channel.message_encoding = "raw";
    channel.metadata = R"({"k":1})";
    channel.native_domain = "device";
    channel.domain_id = 7U;
    channel.domain_epoch = 8U;
    channel.domain_class = nexigen::chrono::TimeDomainClass::DeviceClock;
    channel.access_mode = nexigen::chrono::DeviceAccessMode::ManualPair;
    NX_CHECK(!channel.to_string().empty());
    NX_CHECK(channel.validate().has_value());
    {
        const nlohmann::json j = channel;
        ChannelDeclare back{};
        from_json(j, back);
        NX_CHECK(back.topic == "test/topic" && back.kind == DatasetKind::Video);
        NX_CHECK(back.metadata == channel.metadata);
        NX_CHECK(back.domain_class == nexigen::chrono::TimeDomainClass::DeviceClock);
        NX_CHECK(back.access_mode == nexigen::chrono::DeviceAccessMode::ManualPair);
    }

    SegmentInfo segment{};
    segment.seq = 3U;
    segment.rec_path = "seg.nxrec";
    segment.idx_path = "seg.nxidx";
    segment.vid_path = "seg.nxvid";
    segment.closed = true;
    segment.records = 12U;
    segment.gaps = 1U;
    segment.rec_bytes = 500U;
    segment.vid_bytes = 600U;
    segment.start_commit_ns = 1U;
    segment.end_commit_ns = 2U;
    NX_CHECK(!segment.to_string().empty());
    NX_CHECK(nexigen::recorder::to_string(segment) == segment.to_string());
    {
        const nlohmann::json j = segment;
        SegmentInfo back{};
        from_json(j, back);
        NX_CHECK(back.seq == 3U && back.rec_path == "seg.nxrec");
        NX_CHECK(back.closed && back.records == 12U && back.vid_bytes == 600U);
    }
    SessionManifest manifest{};
    manifest.format = RecorderFormat::Raw;
    manifest.version = kFormatVersion;
    manifest.session_id = "sess";
    manifest.time_domain = TimeDomain::MonotonicRaw;
    manifest.boot_token = "0123456789abcdef";
    manifest.boot_uuid = "01234567-89ab-cdef-0123-456789abcdef";
    manifest.time_base_epoch = 7U;
    manifest.settings.session_root = "/tmp";
    manifest.settings.session_id = "sess";
    manifest.schemas.push_back(schema);
    manifest.channels.push_back(channel);
    manifest.segments.push_back(segment);
    manifest.clean_close = true;
    NX_CHECK(!manifest.to_string().empty());
    NX_CHECK(nexigen::recorder::to_string(manifest) == manifest.to_string());
    {
        const nlohmann::json j = manifest;
        SessionManifest back{};
        from_json(j, back);
        NX_CHECK(back.session_id == "sess" && back.time_domain == TimeDomain::MonotonicRaw);
        NX_CHECK(back.boot_uuid == manifest.boot_uuid && back.time_base_epoch == 7U);
        NX_CHECK(back.schemas.size() == 1U && back.channels.size() == 1U);
        NX_CHECK(back.segments.size() == 1U && back.clean_close);
        NX_CHECK(back.validate().has_value());
    }
}

void check_dataset_serialization() {
    using namespace nexigen::recorder;
    DatasetField field{};
    field.name = "speed";
    field.type = "f32";
    field.units = "m/s";
    field.offset = 4U;
    field.element_size = 4U;
    field.count = 1U;
    field.stride = 4U;
    NX_CHECK(!field.to_string().empty());
    NX_CHECK(nexigen::recorder::to_string(field) == field.to_string());
    {
        const nlohmann::json j = field;
        DatasetField back{};
        from_json(j, back);
        NX_CHECK(back.name == "speed" && back.type == "f32" && back.units == "m/s");
        NX_CHECK(back.offset == 4U && back.stride == 4U);
    }

    DatasetSegmentInfo dataset_segment{};
    dataset_segment.seq = 1U;
    dataset_segment.path = "seg.nxrec";
    dataset_segment.records = 9U;
    dataset_segment.gaps = 0U;
    dataset_segment.bytes = 123U;
    dataset_segment.start_event_ns = 10U;
    dataset_segment.end_event_ns = 20U;
    dataset_segment.closed = true;
    NX_CHECK(!dataset_segment.to_string().empty());
    NX_CHECK(nexigen::recorder::to_string(dataset_segment) == dataset_segment.to_string());
    {
        const nlohmann::json j = dataset_segment;
        DatasetSegmentInfo back{};
        from_json(j, back);
        NX_CHECK(back.seq == 1U && back.path == "seg.nxrec" && back.closed);
        NX_CHECK(back.bytes == 123U && back.end_event_ns == 20U);
    }

    DatasetInfo dataset{};
    dataset.channel_id = 2U;
    dataset.kind = DatasetKind::Telemetry;
    dataset.name = "test/topic";
    dataset.schema_id = 5U;
    dataset.schema_name = "test.Schema";
    dataset.schema_encoding = "jsonschema";
    dataset.message_encoding = "nexigen.pod";
    dataset.native_domain = "host_counter";
    dataset.domain_id = 4U;
    dataset.domain_epoch = 1U;
    dataset.codec = VideoCodec::H265;
    dataset.bitstream = BitstreamFormat::AnnexB;
    dataset.width = 64U;
    dataset.height = 32U;
    dataset.extradata_size = 1U;
    dataset.records = 9U;
    dataset.gaps = 1U;
    dataset.payload_bytes = 100U;
    dataset.first_event_ns = 1U;
    dataset.last_event_ns = 2U;
    dataset.first_rec_offset = 48U;
    dataset.last_rec_offset = 96U;
    dataset.closed = true;
    dataset.config_epochs = {1U, 2U};
    dataset.fields.push_back(field);
    dataset.segments.push_back(dataset_segment);
    NX_CHECK(!dataset.to_string().empty());
    NX_CHECK(nexigen::recorder::to_string(dataset) == dataset.to_string());
    {
        const nlohmann::json j = dataset;
        DatasetInfo back{};
        from_json(j, back);
        NX_CHECK(back.channel_id == 2U && back.name == "test/topic");
        NX_CHECK(back.native_domain == "host_counter" && back.domain_id == 4U);
        NX_CHECK(back.codec == VideoCodec::H265);
        NX_CHECK(back.config_epochs.size() == 2U && back.config_epochs[1] == 2U);
        NX_CHECK(back.fields.size() == 1U && back.fields[0].name == "speed");
        NX_CHECK(back.segments.size() == 1U && back.segments[0].path == "seg.nxrec");
    }
}

void test_recorder_serialization_extended() {
    check_wire_struct_serialization();
    check_declaration_and_manifest_serialization();
    check_dataset_serialization();
}

void test_config_loader_errors() {
    const std::string root = temp_root() + "-cfg-errors";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const auto write = [&root](const std::string& name, const std::string& text) {
        const std::string path = root + "/" + name;
        std::ofstream output(path);
        output << text;
        return path;
    };

    NX_CHECK(!nexigen::recorder::load_recorder_config(root + "/missing.json").has_value());
    if (const auto missing = nexigen::recorder::load_recorder_config(root + "/missing.json");
        !missing.has_value()) {
        NX_CHECK(missing.error() == nexigen::core::ErrorCode::NotFound);
    }

    const auto malformed = write("malformed.json", "{not json");
    NX_CHECK(!nexigen::recorder::load_recorder_config(malformed).has_value());
    const auto no_storage = write("no-storage.json", R"({"schemas":[]})");
    NX_CHECK(!nexigen::recorder::load_recorder_config(no_storage).has_value());
    const auto bad_storage =
        write("bad-storage.json", R"({"storage":{"session_root":"relative","session_id":"s"}})");
    NX_CHECK(!nexigen::recorder::load_recorder_config(bad_storage).has_value());

    const std::string schema_path = write("schema.json", R"({"type":"object"})");
    const auto bad_schema_entry =
        write("bad-schema-entry.json",
              R"({"storage":{"session_root":")" + root + R"(","session_id":"s"},"schemas":[42]})");
    NX_CHECK(!nexigen::recorder::load_recorder_config(bad_schema_entry).has_value());
    const auto no_schema_path =
        write("no-schema-path.json", R"({"storage":{"session_root":")" + root +
                                         R"(","session_id":"s"},"schemas":[{"id":1}]})");
    NX_CHECK(!nexigen::recorder::load_recorder_config(no_schema_path).has_value());
    const auto unreadable_schema =
        write("unreadable-schema.json",
              R"({"storage":{"session_root":")" + root +
                  R"(","session_id":"s"},)"
                  R"("schemas":[{"id":1,"name":"n","encoding":"jsonschema","path":")" +
                  root + R"(/nope.json"}]})");
    NX_CHECK(!nexigen::recorder::load_recorder_config(unreadable_schema).has_value());
    const auto bad_digest = write(
        "bad-digest.json", R"({"storage":{"session_root":")" + root +
                               R"(","session_id":"s"},)"
                               R"("schemas":[{"id":1,"name":"n","encoding":"jsonschema","path":")" +
                               schema_path + R"(","sha256":"deadbeef"}]})");
    const auto digest = nexigen::recorder::load_recorder_config(bad_digest);
    NX_CHECK(!digest.has_value());
    if (!digest.has_value()) {
        NX_CHECK(digest.error() == nexigen::core::ErrorCode::SecurityError);
    }

    const std::string big_schema = root + "/big-schema.json";
    {
        std::ofstream out(big_schema, std::ios::binary);
        std::vector<char> blob(nexigen::recorder::kMaxSchemaBytes + 1U, 'x');
        out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    }
    const auto oversized_schema =
        write("oversized-schema.json",
              R"({"storage":{"session_root":")" + root +
                  R"(","session_id":"s"},)"
                  R"("schemas":[{"id":1,"name":"n","encoding":"jsonschema","path":")" +
                  big_schema + R"("}]})");
    NX_CHECK(!nexigen::recorder::load_recorder_config(oversized_schema).has_value());

    const auto zero_digest =
        write("zero-digest.json",
              R"({"storage":{"session_root":")" + root +
                  R"(","session_id":"s"},)"
                  R"("schemas":[{"id":1,"name":"n","encoding":"jsonschema","path":")" +
                  schema_path + R"(","sha256":")" + std::string(64U, '0') + R"("}]})");
    const auto zero = nexigen::recorder::load_recorder_config(zero_digest);
    NX_CHECK(!zero.has_value());
    if (!zero.has_value()) {
        NX_CHECK(zero.error() == nexigen::core::ErrorCode::SecurityError);
    }

    const auto no_digest = write(
        "no-digest.json", R"({"storage":{"session_root":")" + root +
                              R"(","session_id":"s"},)"
                              R"("schemas":[{"id":1,"name":"n","encoding":"jsonschema","path":")" +
                              schema_path + R"("}]})");
    NX_CHECK(nexigen::recorder::load_recorder_config(no_digest).has_value());

    const auto bad_channel_entry =
        write("bad-channel-entry.json",
              R"({"storage":{"session_root":")" + root + R"(","session_id":"s"},"channels":[7]})");
    NX_CHECK(!nexigen::recorder::load_recorder_config(bad_channel_entry).has_value());
    const auto invalid_channel =
        write("invalid-channel.json",
              R"({"storage":{"session_root":")" + root +
                  R"(","session_id":"s"},)"
                  R"("channels":[{"id":1,"schema_id":1,"topic":"","message_encoding":"raw"}]})");
    NX_CHECK(!nexigen::recorder::load_recorder_config(invalid_channel).has_value());

    std::filesystem::remove_all(root, error);
}

void run_writer_video_paths(const nexigen::recorder::StorageSettings& settings,
                            const nexigen::recorder::SchemaDeclare& schema,
                            const nexigen::recorder::ChannelDeclare& channel) {
    using namespace nexigen::recorder;
    // Video paths: wrong kind, oversized frame, per-frame CRC, and closed writer.
    nexigen::recorder::StorageSettings video_settings = settings;
    video_settings.session_id = "video";
    video_settings.video_crc = nexigen::recorder::VideoCrcMode::PerFrame;
    auto video_writer = nexigen::recorder::SessionWriter::create(video_settings);
    NX_CHECK(video_writer.has_value());
    if (video_writer.has_value()) {
        NX_CHECK(video_writer->declare_schema(schema).has_value());
        nexigen::recorder::ChannelDeclare video_channel = channel;
        video_channel.id = 1U;
        video_channel.kind = nexigen::recorder::DatasetKind::Video;
        NX_CHECK(video_writer->declare_channel(video_channel).has_value());
        nexigen::recorder::VideoStreamConfig stream{};
        stream.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H264);
        stream.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
        stream.width = 16U;
        stream.height = 16U;
        stream.timebase_num = 1U;
        stream.timebase_den = 30U;
        nexigen::recorder::VideoFrameRef reference{};
        reference.capture_mono_ns = 1U;
        const std::array<std::byte, 4> frame{std::byte{1}, std::byte{2}, std::byte{3},
                                             std::byte{4}};
        NX_CHECK(video_writer->append_video(1U, stream, reference, frame).has_value());
        NX_CHECK(!video_writer->append_video(9U, stream, reference, frame).has_value());
        const std::vector<std::byte> huge_frame(5000);
        NX_CHECK(!video_writer->append_video(1U, stream, reference, huge_frame).has_value());
        NX_CHECK(video_writer->close().has_value());
        NX_CHECK(!video_writer->append_video(1U, stream, reference, frame).has_value());
    }
}

void run_writer_move_and_retention_paths(const std::string& root,
                                         const nexigen::recorder::StorageSettings& settings,
                                         const nexigen::recorder::SchemaDeclare& schema,
                                         const nexigen::recorder::ChannelDeclare& channel,
                                         const std::vector<std::byte>& payload) {
    using namespace nexigen::recorder;
    std::error_code error;
    // A moved-from writer reports NotInitialized instead of touching null state.
    auto owner = nexigen::recorder::SessionWriter::create(settings);
    auto moved = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(owner.has_value() && moved.has_value());
    if (owner.has_value() && moved.has_value()) {
        *owner = std::move(*moved);
        NX_CHECK(!moved->declare_schema(schema).has_value());
        NX_CHECK(
            !moved->declare_channel("topic/x", 1U, nexigen::recorder::DatasetKind::Telemetry, "raw")
                 .has_value());
        NX_CHECK(!moved->rotate().has_value());
        nexigen::recorder::RecordShard shard{};
        NX_CHECK(!moved->append_from_shard(shard).has_value());
        static_cast<void>(owner->close());
    }

    // Retention Stop halts cleanly once the retention limit is exceeded.
    nexigen::recorder::StorageSettings stop = settings;
    stop.session_id = "stop";
    stop.retention = nexigen::recorder::RetentionPolicy::Stop;
    stop.retention_max_bytes = 1U;
    auto stop_writer = nexigen::recorder::SessionWriter::create(stop);
    NX_CHECK(stop_writer.has_value());
    if (stop_writer.has_value()) {
        NX_CHECK(stop_writer->declare_schema(schema).has_value());
        NX_CHECK(stop_writer->declare_channel(channel).has_value());
        NX_CHECK(
            stop_writer->append(nexigen::recorder::RecordType::ComPayload, 1U, payload, 1U, 1U, 1U)
                .has_value());
        const auto halted = stop_writer->rotate();
        NX_CHECK(!halted.has_value());
        if (!halted.has_value()) {
            NX_CHECK(halted.error() == nexigen::core::ErrorCode::ResourceExhausted);
        }
        static_cast<void>(stop_writer->close());
    }

    std::filesystem::remove_all(root, error);
}

void test_session_writer_error_paths() {
    using namespace nexigen::recorder;

    std::error_code error;
    const std::string root = temp_root() + "-writer-errors";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings invalid{};
    invalid.session_root = "relative";
    invalid.session_id = "s";
    NX_CHECK(!nexigen::recorder::SessionWriter::create(invalid).has_value());

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = ""; // auto-generated
    settings.session_layout = "{session_id}/{label}";
    settings.min_free_bytes = 0;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 4096;
    settings.video_frame_buffer_bytes = 4096;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    NX_CHECK(!writer->manifest().session_id.empty());
    NX_CHECK(writer->current_segment_seq() == 0U);

    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Errors";
    schema.encoding = "jsonschema";
    schema.data = {1U, 2U, 3U};
    NX_CHECK(writer->declare_schema(schema).has_value());
    nexigen::recorder::SchemaDeclare duplicate = schema;
    duplicate.name = "test.Duplicate";
    NX_CHECK(!writer->declare_schema(duplicate).has_value()); // conflict
    nexigen::recorder::SchemaDeclare invalid_schema{};
    invalid_schema.id = 0;
    NX_CHECK(!writer->declare_schema(invalid_schema).has_value()); // invalid

    nexigen::recorder::ChannelDeclare channel{};
    channel.id = 1;
    channel.schema_id = 1;
    channel.topic = "test/topic";
    channel.message_encoding = "nexigen.pod";
    NX_CHECK(writer->declare_channel(channel).has_value());
    nexigen::recorder::ChannelDeclare duplicate_channel = channel;
    duplicate_channel.topic = "test/other";
    NX_CHECK(!writer->declare_channel(duplicate_channel).has_value()); // conflict id
    nexigen::recorder::ChannelDeclare missing_schema = channel;
    missing_schema.id = 2;
    missing_schema.schema_id = 99;
    NX_CHECK(!writer->declare_channel(missing_schema).has_value()); // not found
    nexigen::recorder::ChannelDeclare invalid_channel = channel;
    invalid_channel.id = 3;
    invalid_channel.topic = "";
    NX_CHECK(!writer->declare_channel(invalid_channel).has_value()); // invalid

    // Declare-by-topic reuses the id for the same pair and allocates a new one.
    const auto first_id = writer->declare_channel(
        "topic/a", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    const auto same_id = writer->declare_channel(
        "topic/a", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    const auto second_id =
        writer->declare_channel("topic/b", 1, nexigen::recorder::DatasetKind::Opaque, "protobuf");
    NX_CHECK(first_id.has_value() && same_id.has_value() && second_id.has_value());
    if (first_id && same_id && second_id) {
        NX_CHECK(*first_id == *same_id);
        NX_CHECK(*first_id != *second_id);
    }

    const std::vector<std::byte> payload(8);
    NX_CHECK(!writer->append(nexigen::recorder::RecordType::ComPayload, 55U, payload, 1U, 1U, 1U)
                  .has_value()); // undeclared channel
    const std::vector<std::byte> oversized(5000);
    NX_CHECK(
        !writer->append(nexigen::recorder::RecordType::ComPayload, *first_id, oversized, 1U, 1U, 1U)
             .has_value()); // above max_record_bytes
    NX_CHECK(
        writer->append(nexigen::recorder::RecordType::ComPayload, *first_id, payload, 1U, 1U, 1U)
            .has_value());

    NX_CHECK(writer->rotate().has_value());
    NX_CHECK(writer->current_segment_seq() == 1U);
    NX_CHECK(writer->close().has_value());
    NX_CHECK(
        !writer->append(nexigen::recorder::RecordType::ComPayload, *first_id, payload, 1U, 1U, 1U)
             .has_value());                  // closed
    NX_CHECK(!writer->rotate().has_value()); // closed
    NX_CHECK(writer->close().has_value());   // idempotent

    // Move assignment transfers ownership.
    auto first_writer = nexigen::recorder::SessionWriter::create(settings);
    auto second_writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(first_writer.has_value() && second_writer.has_value());
    if (first_writer.has_value() && second_writer.has_value()) {
        *first_writer = std::move(*second_writer);
        NX_CHECK(!first_writer->manifest().session_id.empty());
        static_cast<void>(first_writer->close());
    }

    // A directory that cannot be created maps to SystemError.
    nexigen::recorder::StorageSettings bad_root = settings;
    bad_root.session_id = "badroot";
    bad_root.session_root = "/proc/nexigen-no-such";
    const auto denied = nexigen::recorder::SessionWriter::create(bad_root);
    NX_CHECK(!denied.has_value());
    if (!denied.has_value()) {
        NX_CHECK(denied.error() == nexigen::core::ErrorCode::SystemError);
    }

    run_writer_video_paths(settings, schema, channel);
    run_writer_move_and_retention_paths(root, settings, schema, channel, payload);
}

void test_session_reader_error_paths() {
    using nexigen::recorder::RecordHeader;
    using nexigen::recorder::RecordType;
    using nexigen::recorder::SessionReader;
    using nexigen::recorder::SessionWriter;
    using nexigen::recorder::VerifyFailure;

    std::error_code error;
    const std::string root = temp_root() + "-reader-errors";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.min_free_bytes = 0;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 4096;
    auto writer = SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Reader";
    schema.encoding = "jsonschema";
    schema.data = {1U, 2U, 3U};
    NX_CHECK(writer->declare_schema(schema).has_value());
    nexigen::recorder::ChannelDeclare channel{};
    channel.id = 1;
    channel.schema_id = 1;
    channel.topic = "test/topic";
    channel.message_encoding = "nexigen.pod";
    NX_CHECK(writer->declare_channel(channel).has_value());
    const std::vector<std::byte> payload(8, std::byte{0x11});
    NX_CHECK(writer->append(RecordType::ComPayload, 1U, payload, 10U, 0U, 1U).has_value());
    NX_CHECK(writer->append(RecordType::ComPayload, 1U, payload, 20U, 1U, 1U).has_value());
    NX_CHECK(writer->close().has_value());

    const std::string directory = root + "/sess";
    const auto segment = writer->manifest().segments.front();
    const std::string rec = directory + "/" + segment.rec_path;

    // Open failures: absent directory/manifest, malformed and rejected manifests.
    NX_CHECK(!SessionReader::open(root + "/absent").has_value());
    const std::string bad_dir = root + "/bad";
    std::filesystem::create_directories(bad_dir, error);
    {
        std::ofstream out(bad_dir + "/session.json");
        out << "{broken";
    }
    NX_CHECK(!SessionReader::open(bad_dir).has_value());
    {
        std::ofstream out(bad_dir + "/session.json");
        out << R"({"session_id":"x","version":1})";
    }
    NX_CHECK(!SessionReader::open(bad_dir).has_value());

    // Bare-segment open failures.
    NX_CHECK(!SessionReader::open_segment(root + "/missing.nxrec").has_value());
    const std::string empty_segment = root + "/empty_000000.nxrec";
    { std::ofstream out(empty_segment, std::ios::binary); }
    NX_CHECK(!SessionReader::open_segment(empty_segment).has_value());

    auto reader = SessionReader::open(directory);
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    NX_CHECK(!reader->recover_index(99U).has_value());

    std::uint64_t records = 0;
    std::uint64_t gaps = 0;
    VerifyFailure failure{};
    NX_CHECK(reader->verify(records, gaps, &failure).has_value());

    // Locate the first ComPayload payload for corruption.
    std::ifstream input(rec, std::ios::binary);
    std::streamoff payload_offset = -1;
    while (true) {
        const std::streamoff position = input.tellg();
        RecordHeader header{};
        if (!input.read(reinterpret_cast<char*>(&header), sizeof(header))) {
            break;
        }
        if (header.type == static_cast<std::uint16_t>(RecordType::ComPayload)) {
            payload_offset = position + static_cast<std::streamoff>(sizeof(RecordHeader));
            break;
        }
        input.seekg(static_cast<std::streamoff>(header.payload_size), std::ios::cur);
    }
    NX_CHECK(payload_offset > 0);

    std::vector<char> original;
    {
        std::ifstream in(rec, std::ios::binary);
        original.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const auto write_bytes = [&](const std::vector<char>& bytes) {
        std::ofstream out(rec, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };

    // A corrupt payload CRC is diagnosed with its segment and offset.
    auto corrupted = original;
    corrupted[static_cast<std::size_t>(payload_offset)] ^= 0x5A;
    write_bytes(corrupted);
    NX_CHECK(!reader->verify(records, gaps, &failure).has_value());
    NX_CHECK(failure.segment_seq == segment.seq);
    NX_CHECK(failure.offset > 0U);
    write_bytes(original);
    NX_CHECK(reader->verify(records, gaps).has_value());

    // A corrupt footer is diagnosed for the closed segment.
    auto bad_footer = original;
    bad_footer.back() = static_cast<char>(bad_footer.back() ^ 0xFF);
    write_bytes(bad_footer);
    NX_CHECK(!reader->verify(records, gaps, &failure).has_value());
    NX_CHECK(failure.segment_seq == segment.seq);
    write_bytes(original);

    // A missing segment file is diagnosed.
    std::filesystem::rename(rec, rec + ".gone", error);
    NX_CHECK(!error);
    NX_CHECK(!reader->verify(records, gaps, &failure).has_value());
    NX_CHECK(failure.segment_seq == segment.seq);
    std::filesystem::rename(rec + ".gone", rec, error);
    NX_CHECK(!error);

    // Video reads reject an unknown segment, oversized reference, truncated
    // blob and CRC mismatch, and accept a verified frame.
    nexigen::recorder::VideoFrameRef reference{};
    reference.vid_segment_seq = 0U;
    reference.vid_offset = 0U;
    reference.vid_size = 4U;
    {
        std::ofstream vid(directory + "/" + segment.vid_path, std::ios::binary | std::ios::trunc);
        const std::array<char, 4> bytes{1, 2, 3, 4};
        vid.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    NX_CHECK(reader->read_video(reference).has_value());
    reference.vid_crc32c = 0xDEADBEEFU;
    const auto bad_crc = reader->read_video(reference);
    NX_CHECK(!bad_crc.has_value());
    if (!bad_crc.has_value()) {
        NX_CHECK(bad_crc.error() == nexigen::core::ErrorCode::SecurityError);
    }
    reference.vid_crc32c = 0U;
    reference.vid_size = 0xFFFFFFFFU;
    const auto oversized = reader->read_video(reference);
    NX_CHECK(!oversized.has_value());
    if (!oversized.has_value()) {
        NX_CHECK(oversized.error() == nexigen::core::ErrorCode::CorruptData);
    }
    reference.vid_size = 4U;
    reference.vid_segment_seq = 99U;
    const auto unknown = reader->read_video(reference);
    NX_CHECK(!unknown.has_value());
    if (!unknown.has_value()) {
        NX_CHECK(unknown.error() == nexigen::core::ErrorCode::NotFound);
    }

    // A standalone segment opens from its declarations and yields an inventory.
    reference.vid_segment_seq = 0U;
    const std::string standalone = root + "/standalone_000000.nxrec";
    std::filesystem::copy_file(rec, standalone, std::filesystem::copy_options::overwrite_existing,
                               error);
    NX_CHECK(!error);
    auto bare = SessionReader::open_segment(standalone);
    NX_CHECK(bare.has_value());
    if (bare.has_value()) {
        const auto bare_inventory = bare->datasets();
        NX_CHECK(bare_inventory.has_value() && !bare_inventory->empty());
    }
    const auto inventory = reader->datasets();
    NX_CHECK(inventory.has_value() && !inventory->empty());

    // Channel and event-range filters narrow the scan.
    std::uint64_t channel_hits = 0;
    std::uint64_t range_hits = 0;
    auto count_visitor = +[](void* context, const RecordHeader&, std::span<const std::byte>,
                             std::uint64_t) noexcept {
        ++*static_cast<std::uint64_t*>(context);
        return true;
    };
    NX_CHECK(reader->for_each(1U, true, 0U, 0U, false, count_visitor, &channel_hits).has_value());
    NX_CHECK(channel_hits >= 2U);
    channel_hits = 0;
    NX_CHECK(reader->for_each(9U, true, 0U, 0U, false, count_visitor, &channel_hits).has_value());
    NX_CHECK(channel_hits == 0U);
    NX_CHECK(reader
                 ->for_each(0U, false, 0U, std::numeric_limits<std::uint64_t>::max(), true,
                            count_visitor, &range_hits)
                 .has_value());
    NX_CHECK(range_hits >= 1U);
    range_hits = 0;
    NX_CHECK(reader->for_each(0U, false, 0U, 1U, true, count_visitor, &range_hits).has_value());
    NX_CHECK(range_hits == 0U);

    // A missing blob and a truncated blob are named errors.
    std::filesystem::remove(directory + "/" + segment.vid_path, error);
    const auto missing_blob = reader->read_video(reference);
    NX_CHECK(!missing_blob.has_value());
    if (!missing_blob.has_value()) {
        NX_CHECK(missing_blob.error() == nexigen::core::ErrorCode::NotFound);
    }
    {
        std::ofstream vid(directory + "/" + segment.vid_path, std::ios::binary | std::ios::trunc);
        const std::array<char, 2> bytes{1, 2};
        vid.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const auto truncated_blob = reader->read_video(reference);
    NX_CHECK(!truncated_blob.has_value());
    if (!truncated_blob.has_value()) {
        NX_CHECK(truncated_blob.error() == nexigen::core::ErrorCode::SystemError);
    }

    std::filesystem::remove_all(root, error);
}

void test_time_base_resolver_edges() {
    using nexigen::recorder::TimeBasePayload;
    using nexigen::recorder::TimeBaseResolver;

    TimeBasePayload entry{};
    entry.generation = 7U;
    entry.time_base_epoch = 42U;
    entry.boot_token = 0xABCDU;
    entry.status = nexigen::chrono::TimeBaseStatus::Ready;
    entry.domain_count = 2U;
    entry.rows[0].domain_id = 7U;
    entry.rows[0].domain_class = nexigen::chrono::TimeDomainClass::DeviceClock;
    entry.rows[0].lock = nexigen::chrono::LockState::Locked;
    entry.rows[0].epoch = 5U;
    entry.rows[0].native_anchor = 1'000U;
    entry.rows[0].canonical_anchor_ns = 2'000;
    entry.rows[0].scale_fwd_q32 = 1ULL << 32U;
    entry.rows[0].scale_rev_q32 = 1ULL << 32U;
    entry.rows[0].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;
    entry.rows[0].valid_from_native = 0U;
    entry.rows[0].valid_to_native = 10'000U;
    entry.rows[0].valid_from_canonical = 1'000U;
    entry.rows[0].valid_to_canonical = 11'000U;
    entry.rows[1].domain_id = nexigen::chrono::k_realtime_domain_id;
    entry.rows[1].domain_class = nexigen::chrono::TimeDomainClass::SystemClock;
    entry.rows[1].lock = nexigen::chrono::LockState::Locked;
    entry.rows[1].epoch = 1U;
    entry.rows[1].native_anchor = 10'000U;
    entry.rows[1].canonical_anchor_ns = 2'000;
    entry.rows[1].scale_fwd_q32 = 1ULL << 32U;
    entry.rows[1].scale_rev_q32 = 1ULL << 32U;
    entry.rows[1].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;
    entry.rows[1].valid_from_canonical = 1'000U;
    entry.rows[1].valid_to_canonical = 11'000U;

    TimeBaseResolver resolver{};
    NX_CHECK(resolver.add(entry));
    NX_CHECK(resolver.boot_token() == std::optional<std::uint64_t>{0xABCDU});
    NX_CHECK(resolver.time_base_epoch() == std::optional<std::uint64_t>{42U});

    NX_CHECK(resolver.canonical_from_native(nexigen::chrono::k_monotonic_raw_domain_id, 0U, 123U) ==
             std::optional<std::int64_t>{123});
    NX_CHECK(!resolver.canonical_from_native(7U, 6U, 1'500U).has_value());  // epoch mismatch
    NX_CHECK(!resolver.canonical_from_native(7U, 5U, 20'000U).has_value()); // outside validity
    NX_CHECK(!resolver.canonical_from_native(99U, 0U, 1U).has_value());     // unknown domain

    NX_CHECK(!resolver.native_from_canonical(7U, 5U, -1).has_value());
    NX_CHECK(!resolver.native_from_canonical(nexigen::chrono::k_monotonic_raw_domain_id, 0U, -1)
                  .has_value());
    NX_CHECK(!resolver.native_from_canonical(7U, 6U, 2'000).has_value());
    NX_CHECK(!resolver.native_from_canonical(7U, 5U, 50'000).has_value());
    NX_CHECK(resolver.native_from_canonical(7U, 5U, 2'500) == std::optional<std::uint64_t>{1'500U});

    NX_CHECK(resolver.convert(7U, 5U, 1'500U, nexigen::chrono::k_monotonic_raw_domain_id) ==
             std::optional<std::uint64_t>{2'500U});
    NX_CHECK(resolver.convert(7U, 5U, 1'500U, 7U) == std::optional<std::uint64_t>{1'500U});
    NX_CHECK(!resolver.convert(7U, 6U, 1'500U, nexigen::chrono::k_realtime_domain_id).has_value());

    NX_CHECK(resolver.utc_from_canonical(2'500U) == std::optional<std::int64_t>{10'500});
    NX_CHECK(!resolver.utc_from_canonical(50'000U).has_value());

    // A snapshot with a different boot token cannot anchor a native instant.
    nexigen::chrono::TimeSnapshot snapshot{};
    snapshot.generation = 7U;
    snapshot.boot_token = 0xFFFFU;
    snapshot.monotonic_raw_ns = 5'000U;
    snapshot.domain_count = 1U;
    snapshot.domains[0].domain_id = 7U;
    snapshot.domains[0].domain_class = nexigen::chrono::TimeDomainClass::DeviceClock;
    snapshot.domains[0].lock = nexigen::chrono::LockState::Locked;
    snapshot.domains[0].epoch = 5U;
    snapshot.domains[0].value = 4'000U;
    resolver.add_snapshot(snapshot);
    NX_CHECK(!resolver.canonical_from_native(7U, 5U, 20'000U).has_value());

    // Range guards and missing rows on the reverse paths.
    NX_CHECK(!resolver.utc_from_canonical(std::numeric_limits<std::uint64_t>::max()).has_value());
    NX_CHECK(!resolver.tai_from_canonical(std::numeric_limits<std::uint64_t>::max()).has_value());
    NX_CHECK(!resolver.tai_from_canonical(2'500U).has_value()); // no Tai row recorded

    // A matching-boot snapshot whose row does not cover the snapshot instant
    // stays unresolved instead of anchoring an unrelated row.
    TimeBaseResolver uncovered{};
    NX_CHECK(uncovered.add(entry));
    nexigen::chrono::TimeSnapshot outside{};
    outside.generation = 7U;
    outside.boot_token = entry.boot_token;
    outside.monotonic_raw_ns = 50'000U;
    outside.domain_count = 1U;
    outside.domains[0].domain_id = 7U;
    outside.domains[0].domain_class = nexigen::chrono::TimeDomainClass::DeviceClock;
    outside.domains[0].lock = nexigen::chrono::LockState::Locked;
    outside.domains[0].epoch = 5U;
    outside.domains[0].value = 4'000U;
    uncovered.add_snapshot(outside);
    NX_CHECK(!uncovered.canonical_from_native(7U, 5U, 20'000U).has_value());

    // A matching snapshot whose domain value belongs to another epoch is not
    // used as an anchor.
    TimeBaseResolver epoch_mismatch{};
    NX_CHECK(epoch_mismatch.add(entry));
    nexigen::chrono::TimeSnapshot wrong_epoch = outside;
    wrong_epoch.boot_token = entry.boot_token;
    wrong_epoch.monotonic_raw_ns = 5'000U;
    wrong_epoch.domains[0].epoch = 99U;
    epoch_mismatch.add_snapshot(wrong_epoch);
    NX_CHECK(!epoch_mismatch.canonical_from_native(7U, 5U, 20'000U).has_value());

    // A gap entry never anchors a map.
    TimeBasePayload gap_entry = entry;
    gap_entry.generation = 8U;
    gap_entry.gap = 1U;
    gap_entry.domain_count = 0U;
    TimeBaseResolver with_gap{};
    NX_CHECK(with_gap.add(gap_entry));
    NX_CHECK(!with_gap.canonical_from_native(7U, 5U, 1'500U).has_value());
}

void test_declaration_validation_errors() {
    using namespace nexigen::recorder;

    StorageSettings settings{};
    NX_CHECK(settings.validate().has_value());
    settings.format = static_cast<RecorderFormat>(200);
    NX_CHECK(!settings.validate().has_value());
    settings = StorageSettings{};
    settings.session_root = "relative";
    NX_CHECK(!settings.validate().has_value());
    settings = StorageSettings{};
    settings.session_id = ".";
    NX_CHECK(!settings.validate().has_value());
    settings = StorageSettings{};
    settings.session_id = "bad id";
    NX_CHECK(!settings.validate().has_value());
    settings = StorageSettings{};
    settings.session_id = "../evil";
    NX_CHECK(!settings.validate().has_value());
    settings = StorageSettings{};
    settings.session_layout = "/absolute";
    NX_CHECK(!settings.validate().has_value());
    settings = StorageSettings{};
    settings.session_layout = "{bogus}";
    NX_CHECK(!settings.validate().has_value());
    settings = StorageSettings{};
    settings.session_layout = "{session_id";
    NX_CHECK(!settings.validate().has_value());
    settings = StorageSettings{};
    settings.write_buffer_bytes = 0;
    NX_CHECK(!settings.validate().has_value());

    SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Valid";
    schema.encoding = "jsonschema";
    schema.data = {1U, 2U, 3U};
    NX_CHECK(schema.validate().has_value());
    SchemaDeclare bad_schema = schema;
    bad_schema.id = 0;
    NX_CHECK(!bad_schema.validate().has_value());
    bad_schema = schema;
    bad_schema.encoding = "bogus";
    NX_CHECK(!bad_schema.validate().has_value());
    bad_schema = schema;
    bad_schema.data.assign(kMaxSchemaBytes + 1U, 0U);
    NX_CHECK(!bad_schema.validate().has_value());
    bad_schema = schema;
    bad_schema.name.clear();
    NX_CHECK(!bad_schema.validate().has_value());

    ChannelDeclare channel{};
    channel.id = 1;
    channel.schema_id = 1;
    channel.topic = "test/topic";
    channel.message_encoding = "raw";
    NX_CHECK(channel.validate().has_value());
    ChannelDeclare bad_channel = channel;
    bad_channel.id = 0;
    NX_CHECK(!bad_channel.validate().has_value());
    bad_channel = channel;
    bad_channel.topic.clear();
    NX_CHECK(!bad_channel.validate().has_value());
    bad_channel = channel;
    bad_channel.message_encoding.clear();
    NX_CHECK(!bad_channel.validate().has_value());
    bad_channel = channel;
    bad_channel.native_domain.clear();
    NX_CHECK(!bad_channel.validate().has_value());
    bad_channel = channel;
    bad_channel.native_domain = "quantum";
    NX_CHECK(!bad_channel.validate().has_value());

    SessionManifest manifest{};
    manifest.session_id = "sess";
    manifest.settings.session_root = "/tmp";
    manifest.settings.session_id = "sess";
    NX_CHECK(manifest.validate().has_value());
    manifest.version = 1U;
    NX_CHECK(!manifest.validate().has_value());

    manifest = SessionManifest{};
    manifest.session_id = "sess";
    manifest.settings.session_root = "/tmp";
    manifest.settings.session_id = "sess";
    manifest.schemas.assign(65U, schema);
    NX_CHECK(!manifest.validate().has_value()); // too many schemas
    manifest.schemas = {schema, schema};
    NX_CHECK(!manifest.validate().has_value()); // duplicate schema ids
    manifest.schemas = {SchemaDeclare{}};
    NX_CHECK(!manifest.validate().has_value()); // invalid schema entry
    manifest.schemas = {schema};
    manifest.schemas = {schema};
    manifest.channels = {channel};
    manifest.channels[0].schema_id = 9U;
    NX_CHECK(!manifest.validate().has_value()); // missing schema
    manifest.channels[0].schema_id = 1U;
    manifest.channels.push_back(manifest.channels[0]);
    NX_CHECK(!manifest.validate().has_value()); // duplicate channel ids
    manifest.channels = std::vector<ChannelDeclare>(65U, channel);
    NX_CHECK(!manifest.validate().has_value()); // too many channels
    manifest.channels = {ChannelDeclare{}};
    NX_CHECK(!manifest.validate().has_value()); // invalid channel entry

    manifest = SessionManifest{};
    manifest.session_id = "sess";
    manifest.settings.session_root = "/tmp";
    manifest.settings.session_id = "sess";
    SegmentInfo segment{};
    segment.seq = 0U;
    segment.rec_path = "a.nxrec";
    segment.idx_path = "a.nxidx";
    segment.vid_path = "a.nxvid";
    manifest.segments = {segment, segment};
    NX_CHECK(!manifest.validate().has_value()); // duplicate segment seq
    manifest.segments = {segment};
    manifest.segments[0].rec_path.clear();
    NX_CHECK(!manifest.validate().has_value());
    manifest.segments = {segment};
    manifest.segments[0].rec_path = "a.nxrec";
    manifest.settings.session_root = "relative";
    NX_CHECK(!manifest.validate().has_value());
}

void test_payload_decoder_edges() {
    using nexigen::recorder::DatasetField;

    auto field = [](const char* name, const char* type, std::uint32_t offset, std::uint32_t size,
                    std::uint32_t count, std::uint32_t stride) {
        DatasetField out{};
        out.name = name;
        out.type = type;
        out.offset = offset;
        out.element_size = size;
        out.count = count;
        out.stride = stride;
        return out;
    };

    // A positive 64-bit value takes the no-sign-extension branch.
    std::array<std::byte, 8> positive{};
    positive[0] = std::byte{0x04};
    const auto positive_view = nexigen::recorder::decode_payload(
        "jsonschema", {}, std::vector<DatasetField>{field("p64", "i64", 0U, 8U, 1U, 8U)}, positive);
    NX_CHECK(positive_view.has_value());
    if (positive_view.has_value()) {
        const auto document = nlohmann::json::parse(positive_view->text, nullptr, false);
        NX_CHECK(document["p64"] == 4);
    }

    // An array element outside the payload and an empty field name are corrupt.
    std::array<std::byte, 8> small{};
    const auto array_overrun = nexigen::recorder::decode_payload(
        "jsonschema", {}, std::vector<DatasetField>{field("arr", "u32", 6U, 4U, 2U, 4U)}, small);
    NX_CHECK(!array_overrun.has_value());
    if (!array_overrun.has_value()) {
        NX_CHECK(array_overrun.error() == nexigen::core::ErrorCode::CorruptData);
    }
    const auto empty_name = nexigen::recorder::decode_payload(
        "jsonschema", {}, std::vector<DatasetField>{field("", "u8", 0U, 1U, 1U, 1U)}, small);
    NX_CHECK(!empty_name.has_value());
    if (!empty_name.has_value()) {
        NX_CHECK(empty_name.error() == nexigen::core::ErrorCode::CorruptData);
    }

    // Malformed FileDescriptorSet variants are rejected by the validator.
    const std::array<std::byte, 2> payload{std::byte{0x08}, std::byte{0x01}};
    const std::array<std::vector<std::uint8_t>, 6> bad_descriptors{{
        {0x0BU},             // unknown wire type 3
        {0x0CU},             // unknown wire type 4
        {0x08U},             // varint without a value
        {0x09U, 1U, 2U, 3U}, // fixed64 truncated
        {0x0DU, 1U, 2U},     // fixed32 truncated
        {0x0AU, 0x05U, 'a'}, // length-delimited truncated
    }};
    for (const auto& descriptor : bad_descriptors) {
        const auto view = nexigen::recorder::decode_payload("protobuf", descriptor, {}, payload);
        NX_CHECK(!view.has_value());
        if (!view.has_value()) {
            NX_CHECK(view.error() == nexigen::core::ErrorCode::CorruptData);
        }
    }
    // Structurally valid descriptors covering varint, fixed64 and fixed32
    // wire types.
    const std::array<std::vector<std::uint8_t>, 3> wire_descriptors{{
        {0x08U, 0x01U},
        {0x09U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U},
        {0x0DU, 1U, 2U, 3U, 4U},
    }};
    for (const auto& descriptor : wire_descriptors) {
        static_cast<void>(nexigen::recorder::decode_payload("protobuf", descriptor, {}, payload));
    }

    const std::array<std::uint8_t, 2> short_flatbuffer{0x00U, 0xFFU};
    const auto flat =
        nexigen::recorder::decode_payload("flatbuffer", short_flatbuffer, {}, payload);
    NX_CHECK(!flat.has_value());
    if (!flat.has_value()) {
        NX_CHECK(flat.error() == nexigen::core::ErrorCode::CorruptData);
    }
}

void test_settings_and_payload_hostile_json() {
    nexigen::recorder::StorageSettings settings{};
    const auto base = settings;
    nexigen::recorder::from_json(nlohmann::json(42), settings);
    NX_CHECK(settings.session_root == base.session_root);
    nexigen::recorder::from_json(nlohmann::json{{"format", 5},
                                                {"session_root", 7},
                                                {"session_id", true},
                                                {"session_layout", 1},
                                                {"time_domain", "bogus"},
                                                {"max_segment_bytes", "x"},
                                                {"boot_token", 5},
                                                {"boot_uuid", false},
                                                {"time_base_epoch", "y"}},
                                 settings);
    NX_CHECK(settings.session_root == base.session_root);
    NX_CHECK(settings.format == base.format);

    nexigen::recorder::TimeBasePayload payload{};
    nexigen::recorder::from_json(nlohmann::json(42), payload);
    NX_CHECK(payload.generation == 0U);
    nlohmann::json rows = nlohmann::json::array();
    for (int i = 0; i < 40; ++i) {
        rows.push_back(nlohmann::json{{"domain_id", i}});
    }
    nexigen::recorder::from_json(nlohmann::json{{"rows", rows}, {"domain_count", 40U}}, payload);
    NX_CHECK(payload.domain_count <= 32U);
    nexigen::recorder::from_json(nlohmann::json{{"rows", 5}}, payload);
    nexigen::recorder::DatasetInfo hostile_dataset{};
    nexigen::recorder::from_json(nlohmann::json{{"fields", 5}, {"segments", "x"}}, hostile_dataset);
    NX_CHECK(hostile_dataset.fields.empty() && hostile_dataset.segments.empty());
}

void test_open_segment_declaration_errors() {
    using nexigen::recorder::RecordHeader;
    using nexigen::recorder::SessionReader;
    using nexigen::recorder::SessionWriter;

    std::error_code error;
    const std::string root = temp_root() + "-decl";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.min_free_bytes = 0;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 4096;
    auto writer = SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Decl";
    schema.encoding = "jsonschema";
    schema.data = {1U, 2U, 3U};
    NX_CHECK(writer->declare_schema(schema).has_value());
    NX_CHECK(writer->close().has_value());
    const std::string rec = root + "/sess/" + writer->manifest().segments.front().rec_path;

    std::vector<char> original;
    {
        std::ifstream in(rec, std::ios::binary);
        original.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    NX_CHECK(original.size() > sizeof(RecordHeader));

    const auto write_case = [&](const std::string& name, const std::vector<char>& bytes) {
        const std::string path = root + "/" + name + "_000000.nxrec";
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return path;
    };
    // Corrupt declaration payload CRC.
    auto bad_crc = original;
    bad_crc[sizeof(RecordHeader)] = static_cast<char>(bad_crc[sizeof(RecordHeader)] ^ 0x5A);
    const auto bad_crc_path = write_case("badcrc", bad_crc);
    const auto bad_crc_open = SessionReader::open_segment(bad_crc_path);
    NX_CHECK(!bad_crc_open.has_value());
    if (!bad_crc_open.has_value()) {
        NX_CHECK(bad_crc_open.error() == nexigen::core::ErrorCode::SecurityError);
    }

    // A valid header and CRC over an invalid declaration document.
    {
        RecordHeader header{};
        header.type = static_cast<std::uint16_t>(nexigen::recorder::RecordType::SchemaDeclare);
        header.payload_size = 1U;
        std::vector<char> bytes(sizeof(RecordHeader) + 1U, '{');
        header.payload_crc32c = nexigen::core::integrity::crc32c(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(bytes.data()) + sizeof(RecordHeader), 1U));
        header.header_crc32c = nexigen::core::integrity::crc32c(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(&header), offsetof(RecordHeader, header_crc32c)));
        std::memcpy(bytes.data(), &header, sizeof(header));
        const auto bad_json_path = write_case("badjson", bytes);
        const auto bad_json_open = SessionReader::open_segment(bad_json_path);
        NX_CHECK(!bad_json_open.has_value());
        if (!bad_json_open.has_value()) {
            NX_CHECK(bad_json_open.error() == nexigen::core::ErrorCode::CorruptData);
        }
    }

    // A declaration larger than the reader cap.
    auto oversized = original;
    {
        RecordHeader header{};
        std::memcpy(&header, oversized.data(), sizeof(header));
        header.payload_size = 64U * 1024U * 1024U + 1U;
        header.header_crc32c = nexigen::core::integrity::crc32c(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(&header), offsetof(RecordHeader, header_crc32c)));
        std::memcpy(oversized.data(), &header, sizeof(header));
    }
    const auto oversized_path = write_case("oversized", oversized);
    const auto oversized_open = SessionReader::open_segment(oversized_path);
    NX_CHECK(!oversized_open.has_value());
    if (!oversized_open.has_value()) {
        NX_CHECK(oversized_open.error() == nexigen::core::ErrorCode::CorruptData);
    }

    // Truncated declaration payload.
    auto truncated = original;
    truncated.resize(sizeof(RecordHeader) + 2U);
    const auto truncated_path = write_case("truncated", truncated);
    const auto truncated_open = SessionReader::open_segment(truncated_path);
    NX_CHECK(!truncated_open.has_value());
    if (!truncated_open.has_value()) {
        NX_CHECK(truncated_open.error() == nexigen::core::ErrorCode::CorruptData);
    }

    std::filesystem::remove_all(root, error);
}

void test_reader_video_and_manifest_errors() {
    using nexigen::recorder::SessionReader;
    using nexigen::recorder::SessionWriter;

    std::error_code error;
    const std::string root = temp_root() + "-video-errors";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.min_free_bytes = 0;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.max_record_bytes = 4096;
    settings.video_frame_buffer_bytes = 4096;
    settings.video_crc = nexigen::recorder::VideoCrcMode::PerFrame;
    auto writer = SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1U;
    schema.name = "test.Video";
    schema.encoding = "jsonschema";
    schema.data = {1U, 2U, 3U};
    NX_CHECK(writer->declare_schema(schema).has_value());
    nexigen::recorder::ChannelDeclare channel{};
    channel.id = 1U;
    channel.schema_id = 1U;
    channel.topic = "test/video";
    channel.kind = nexigen::recorder::DatasetKind::Video;
    channel.message_encoding = "raw";
    NX_CHECK(writer->declare_channel(channel).has_value());

    nexigen::recorder::VideoStreamConfig stream{};
    stream.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H264);
    stream.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
    stream.width = 16U;
    stream.height = 16U;
    stream.timebase_num = 1U;
    stream.timebase_den = 30U;
    const std::array<std::byte, 4> frame{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    nexigen::recorder::VideoFrameRef first{};
    first.capture_mono_ns = 1U;
    first.source_sequence = 0U;
    NX_CHECK(writer->append_video(1U, stream, first, frame).has_value());
    nexigen::recorder::VideoFrameRef second = first;
    second.capture_mono_ns = 2U;
    second.source_sequence = 5U; // a gap marker is emitted between the frames
    NX_CHECK(writer->append_video(1U, stream, second, frame).has_value());
    NX_CHECK(writer->close().has_value());

    const std::string directory = root + "/sess";
    const auto segment = writer->manifest().segments.front();
    const std::string rec = directory + "/" + segment.rec_path;
    const std::string vid = directory + "/" + segment.vid_path;

    auto reader = SessionReader::open(directory);
    NX_CHECK(reader.has_value());
    if (!reader.has_value()) {
        return;
    }
    const auto inventory = reader->datasets();
    NX_CHECK(inventory.has_value() && !inventory->empty());

    // Rebuilding the index scans the video reference payloads.
    std::filesystem::remove(directory + "/" + segment.idx_path, error);
    const auto rebuilt = reader->recover_index(segment.seq);
    NX_CHECK(rebuilt.has_value());

    std::uint64_t records = 0;
    std::uint64_t gaps = 0;
    nexigen::recorder::VerifyFailure failure{};
    NX_CHECK(reader->verify(records, gaps, &failure).has_value());

    // A corrupted frame blob makes verification report a security failure.
    {
        std::fstream vid_stream(vid, std::ios::binary | std::ios::in | std::ios::out);
        vid_stream.seekp(0, std::ios::beg);
        vid_stream.put(static_cast<char>(0x7F));
    }
    NX_CHECK(!reader->verify(records, gaps, &failure).has_value());
    if (!reader->verify(records, gaps, &failure).has_value()) {
        NX_CHECK(reader->verify(records, gaps, &failure).error() ==
                 nexigen::core::ErrorCode::SecurityError);
    }
    {
        std::ofstream vid_out(vid, std::ios::binary | std::ios::trunc);
        vid_out.write(reinterpret_cast<const char*>(frame.data()),
                      static_cast<std::streamsize>(frame.size()));
    }

    // A manifest whose record count disagrees with the stream is corrupt.
    const std::string manifest_path = directory + "/session.json";
    nlohmann::json document;
    {
        std::ifstream in(manifest_path);
        document = nlohmann::json::parse(in, nullptr, false);
    }
    NX_CHECK(document.is_object());
    if (document.is_object() && document.contains("segments") && document["segments"].is_array() &&
        !document["segments"].empty()) {
        document["segments"][0]["records"] =
            document["segments"][0]["records"].get<std::uint64_t>() + 1U;
        std::ofstream out(manifest_path, std::ios::trunc);
        out << document.dump();
    }
    auto mismatch = SessionReader::open(directory);
    NX_CHECK(mismatch.has_value());
    if (mismatch.has_value()) {
        NX_CHECK(!mismatch->verify(records, gaps, &failure).has_value());
    }
    std::filesystem::remove_all(root, error);
}

void test_export_selection_roundtrip() {
    nexigen::recorder::ExportSelection selection{};
    selection.dataset = "telemetry";
    selection.channels = {"test/camera", "7"};
    selection.start_event_ns = 100;
    selection.end_event_ns = 200;
    selection.has_start = true;
    selection.has_end = true;
    const nlohmann::json j = selection;
    nexigen::recorder::ExportSelection decoded{};
    nexigen::recorder::from_json(j, decoded);
    NX_CHECK(decoded.dataset == selection.dataset);
    NX_CHECK(decoded.channels == selection.channels);
    NX_CHECK(decoded.start_event_ns == 100);
    NX_CHECK(decoded.end_event_ns == 200);
    NX_CHECK(decoded.has_start && decoded.has_end);
    NX_CHECK(!selection.to_string().empty());
    nexigen::recorder::VideoCodecTag codec = nexigen::recorder::VideoCodec::H265;
    nexigen::recorder::VideoGapReason reason = nexigen::recorder::GapReason::IoBackpressure;
    NX_CHECK(nexigen::recorder::to_string(codec) == "H265");
    NX_CHECK(nexigen::recorder::to_string(reason) == "IoBackpressure");
}

} // namespace

int main() {
    test_wire_layout();
    test_settings_roundtrip();
    test_write_read_roundtrip();
    test_retention_and_truncation();
    test_video_gap();
    test_video_rotation_bytes();
    test_video_refs_survive_retention();
    test_crc32c_hardware_matches_table();
    test_index_channel_mask();
    test_low_disk_rotation();
    test_out_of_order_events();
    test_large_video_frame();
    test_encoder_restart_not_gap();
    test_multi_segment_video_refs();
    test_builtins_and_config();
    test_builtin_frame_descriptors();
    test_torn_every_byte();
    test_shard();
    test_payload_decoder();
    test_opaque_channel_decode();
    test_publisher_epochs();
    test_segment_only_open();
    test_shard_overflow_gap();
    test_export_selection_roundtrip();
    test_time_sync_record();
    test_time_base_history();
    test_recorder_serialization_extended();
    test_config_loader_errors();
    test_session_writer_error_paths();
    test_session_reader_error_paths();
    test_time_base_resolver_edges();
    test_declaration_validation_errors();
    test_payload_decoder_edges();
    test_settings_and_payload_hostile_json();
    test_open_segment_declaration_errors();
    test_reader_video_and_manifest_errors();
    test_payload_decoder_extended();
    test_time_base_session_bridge();
    std::printf("recorder tests: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
