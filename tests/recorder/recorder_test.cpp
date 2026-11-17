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

#include <nexigen/recorder/dataset_info.hpp>
#include <nexigen/recorder/builtin_schemas.hpp>
#include <nexigen/recorder/config_loader.hpp>
#include <nexigen/recorder/export_selection.hpp>
#include <nexigen/recorder/payload_decoder.hpp>
#include <nexigen/core/integrity/sha256.hpp>
#include <nexigen/core/integrity/crc32c.hpp>
#include <nexigen/recorder/session_manifest.hpp>
#include <nexigen/recorder/session_reader.hpp>
#include <nexigen/recorder/session_writer.hpp>
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

void test_wire_layout() {
    static_assert(sizeof(nexigen::recorder::RecordHeader) == 48U);
    static_assert(sizeof(nexigen::recorder::IndexEntry) == 64U);
    static_assert(sizeof(nexigen::recorder::VideoFrameRef) == 64U);
    static_assert(sizeof(nexigen::recorder::VideoStreamConfig) == 1056U);
    static_assert(sizeof(nexigen::recorder::SegmentFooter) == 64U);
    static_assert(sizeof(nexigen::recorder::GapMarkerPayload) == 24U);
    static_assert(sizeof(nexigen::recorder::TimeSyncPayload) == 16U);
    NX_CHECK(nexigen::recorder::to_string(nexigen::recorder::VideoCrcMode::Off) == "Off");
    NX_CHECK(nexigen::recorder::to_string(nexigen::recorder::DatasetKind::Video) == "Video");
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
        reference.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
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
                            std::span<const std::byte> /*payload*/,
                            std::uint64_t /*offset*/) {
        ++static_cast<std::uint64_t*>(context)[0];
        return true;
    };
    NX_CHECK(reader->for_each(1, true, 0, 0, false, visitor, &visited).has_value());
    NX_CHECK(visited == 120);

    std::vector<nexigen::recorder::VideoFrameRef> references;
    const auto ref_visitor = [](void* context, const nexigen::recorder::RecordHeader& header,
                                std::span<const std::byte> record_payload,
                                std::uint64_t /*offset*/) {
        if (header.type == static_cast<std::uint16_t>(
                                nexigen::recorder::RecordType::VideoFrameRef) &&
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
    std::filesystem::remove(reader->directory() + "/" +
                                reader->manifest().segments.front().idx_path,
                            error);
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
    const auto first_id = writer->declare_channel("test/camera", 1,
                                                  nexigen::recorder::DatasetKind::Video,
                                                  "nexigen.pod");
    NX_CHECK(first_id.has_value() && *first_id == 1);
    const auto second_id = writer->declare_channel("test/camera", 2,
                                                   nexigen::recorder::DatasetKind::Video,
                                                   "nexigen.pod");
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
            const auto found = std::find_if(datasets->begin(), datasets->end(),
                                            [](const nexigen::recorder::DatasetInfo& info) {
                                                return info.channel_id == 1;
                                            });
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
    const auto channel = writer->declare_channel("test/camera", 1,
                                                 nexigen::recorder::DatasetKind::Video,
                                                 "nexigen.pod");
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
    const auto channel = writer->declare_channel("test/camera", 1,
                                                 nexigen::recorder::DatasetKind::Video,
                                                 "nexigen.pod");
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
        if (header.type != static_cast<std::uint16_t>(
                                nexigen::recorder::RecordType::VideoFrameRef)) {
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
    const auto channel = writer->declare_channel("test/crc", 1,
                                                 nexigen::recorder::DatasetKind::Telemetry,
                                                 "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::array<std::size_t, 24> sizes{0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,
                                            8U,  9U,  15U, 16U, 17U, 31U, 32U, 33U,
                                            63U, 64U, 65U, 127U, 255U, 511U, 1024U, 4096U};
    for (std::uint64_t i = 0; i < sizes.size(); ++i) {
        std::vector<std::byte> payload(sizes[i]);
        for (std::size_t b = 0; b < payload.size(); ++b) {
            payload[b] = static_cast<std::byte>((b * 31U + i * 7U) & 0xffU);
        }
        NX_CHECK(writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload,
                                i, i, 1)
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
        const auto header_bytes = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(&header),
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
    const auto first = writer->declare_channel("test/one", 1,
                                               nexigen::recorder::DatasetKind::Telemetry,
                                               "nexigen.pod");
    const auto second = writer->declare_channel("test/two", 1,
                                                nexigen::recorder::DatasetKind::Telemetry,
                                                "nexigen.pod");
    NX_CHECK(first.has_value() && second.has_value());
    if (!first.has_value() || !second.has_value()) {
        return;
    }
    const std::vector<std::byte> payload(8);
    for (std::uint32_t id : {*first, *second, *first}) {
        NX_CHECK(writer->append(nexigen::recorder::RecordType::ComPayload, id, payload, id,
                                id, 1)
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
    const auto channel = writer->declare_channel("test/disk", 1,
                                                 nexigen::recorder::DatasetKind::Telemetry,
                                                 "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::vector<std::byte> payload(16);
    const auto blocked = writer->append(nexigen::recorder::RecordType::ComPayload, *channel,
                                        payload, 0U, 0U, 1);
    NX_CHECK(!blocked.has_value());
    NX_CHECK(blocked.error() == nexigen::core::ErrorCode::ResourceExhausted);
    NX_CHECK(writer->manifest().clean_close);
    NX_CHECK(writer->manifest().segments.size() == 1U);
    NX_CHECK(writer->manifest().segments.front().closed);
    NX_CHECK(!writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload,
                             3U, 3U, 1)
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
    const auto channel = writer->declare_channel("test/order", 1,
                                                 nexigen::recorder::DatasetKind::Telemetry,
                                                 "nexigen.pod");
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
        NX_CHECK(writer->append(
                     nexigen::recorder::RecordType::VideoFrameRef, *channel,
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
    const auto channel = writer->declare_channel("test/large", 1,
                                                 nexigen::recorder::DatasetKind::Video,
                                                 "nexigen.pod");
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
        if (header.type != static_cast<std::uint16_t>(
                                nexigen::recorder::RecordType::VideoFrameRef)) {
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
    const auto channel = writer->declare_channel("test/restart", 1,
                                                 nexigen::recorder::DatasetKind::Video,
                                                 "nexigen.pod");
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
    const auto channel = writer->declare_channel("test/camera", 1,
                                                 nexigen::recorder::DatasetKind::Video,
                                                 "nexigen.pod");
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
            if (header.type != static_cast<std::uint16_t>(
                                    nexigen::recorder::RecordType::VideoFrameRef)) {
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

void test_builtins_and_config() {
    const auto schemas = nexigen::recorder::builtin_schemas();
    NX_CHECK(schemas.size() == 7);
    bool log_found = false;
    for (const auto& schema : schemas) {
        if (schema.name == "nexigen.log.LogFrame") {
            log_found = true;
            NX_CHECK(!schema.data.empty());
            NX_CHECK(schema.data_crc32c != 0U);
        }
    }
    NX_CHECK(log_found);

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
               << R"("path":")" << schema_path << R"(","sha256":")" << digest
               << R"("}],)"
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
                                std::span<const std::byte> /*payload*/,
                                std::uint64_t /*offset*/) {
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
                std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t*>(&header), 40U));
            std::memcpy(mutated.data() + i, &header, sizeof(header));
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(mutated.data(), static_cast<std::streamsize>(mutated.size()));
            output.close();
            std::uint64_t visited = 0;
            const auto visitor = [](void* context, const nexigen::recorder::RecordHeader& /*h*/,
                                    std::span<const std::byte> /*p*/,
                                    std::uint64_t /*o*/) {
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
    NX_CHECK(writer->declare_channel("test/shard", 1, nexigen::recorder::DatasetKind::Telemetry,
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
                                 std::byte{'k'}, std::byte{0}, std::byte{0}};
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
    const auto short_pod = nexigen::recorder::decode_payload(
        "jsonschema", {}, std::vector<DatasetField>{value},
        std::span<const std::byte>(pod).first(1));
    NX_CHECK(!short_pod.has_value());

    // A minimal FileDescriptorSet passes descriptor validation and stays opaque.
    const std::array<std::uint8_t, 14> descriptor{
        0x0AU, 0x0CU, 0x0AU, 0x0AU, 't',  'e',  's', 't', '.', 'p', 'r', 'o', 't', 'o'};
    std::array<std::byte, 3> message{std::byte{0x08}, std::byte{0x2A}, std::byte{0x00}};
    const auto proto_view =
        nexigen::recorder::decode_payload("protobuf", descriptor, {}, message);
    NX_CHECK(proto_view.has_value());
    if (proto_view.has_value()) {
        NX_CHECK(proto_view->kind == PayloadKind::Opaque);
        NX_CHECK(!proto_view->text.empty());
    }
    const std::array<std::uint8_t, 4> bad_descriptor{0xFFU, 0xFFU, 0xFFU, 0xFFU};
    NX_CHECK(!nexigen::recorder::decode_payload("protobuf", bad_descriptor, {}, message)
                  .has_value());

    // A structurally valid .bfbs schema also passes through opaque.
    const std::array<std::uint8_t, 20> bfbs{
        0x0CU, 0x00U, 0x00U, 0x00U, 0x04U, 0x00U, 0x08U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x08U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    const auto bfbs_view = nexigen::recorder::decode_payload("flatbuffer", bfbs, {}, message);
    NX_CHECK(bfbs_view.has_value());
    if (bfbs_view.has_value()) {
        NX_CHECK(bfbs_view->kind == PayloadKind::Opaque);
    }
    NX_CHECK(!nexigen::recorder::decode_payload("flatbuffer", bad_descriptor, {}, message)
                  .has_value());

    // Unknown encodings report a named error.
    const auto unknown = nexigen::recorder::decode_payload("capnproto", {}, {}, message);
    NX_CHECK(!unknown.has_value());
    if (!unknown.has_value()) {
        NX_CHECK(unknown.error() == nexigen::core::ErrorCode::UnsupportedFormat);
    }
    NX_CHECK(nexigen::recorder::encoding_supported("protobuf"));
    NX_CHECK(!nexigen::recorder::encoding_supported("capnproto"));
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
    const std::array<std::uint8_t, 14> descriptor{
        0x0AU, 0x0CU, 0x0AU, 0x0AU, 't',  'e',  's', 't', '.', 'p', 'r', 'o', 't', 'o'};
    schema.data.assign(descriptor.begin(), descriptor.end());
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel("sensor/opaque", 1,
                                                 nexigen::recorder::DatasetKind::Opaque,
                                                 "protobuf");
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
                            std::span<const std::byte> record_payload,
                            std::uint64_t /*offset*/) {
        auto& local = *static_cast<DecodeState*>(context);
        const auto view = nexigen::recorder::decode_payload("protobuf", local.descriptor, {},
                                                            record_payload);
        local.ok = view.has_value() &&
                   view->kind == nexigen::recorder::PayloadKind::Opaque && !view->text.empty();
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
    const auto channel = writer->declare_channel("test/epoch", 1,
                                                 nexigen::recorder::DatasetKind::Telemetry,
                                                 "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::vector<std::byte> payload(16);
    for (std::uint64_t sequence = 0; sequence < 3; ++sequence) {
        NX_CHECK(writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload,
                                sequence, sequence, 7)
                     .has_value());
    }
    for (std::uint64_t sequence = 0; sequence < 2; ++sequence) {
        NX_CHECK(writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload,
                                sequence + 3, sequence, 8)
                     .has_value());
    }
    NX_CHECK(writer->close().has_value());
    auto reader = nexigen::recorder::SessionReader::open(root + "/sess");
    NX_CHECK(reader.has_value());
    if (reader.has_value()) {
        const auto epochs = reader->publisher_epochs();
        NX_CHECK(epochs.has_value() && epochs->size() == 2);
        if (epochs.has_value() && epochs->size() == 2) {
            NX_CHECK((*epochs)[0].publisher_id == 7);
            NX_CHECK((*epochs)[0].first_sequence == 0);
            NX_CHECK((*epochs)[0].last_sequence == 2);
            NX_CHECK((*epochs)[0].records == 3);
            NX_CHECK((*epochs)[1].publisher_id == 8);
            NX_CHECK((*epochs)[1].records == 2);
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
    const auto channel = writer->declare_channel("test/segment", 1,
                                                 nexigen::recorder::DatasetKind::Telemetry,
                                                 "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::vector<std::byte> payload(32);
    for (std::uint64_t i = 0; i < 3; ++i) {
        NX_CHECK(writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload, i,
                                i, 1)
                     .has_value());
    }
    NX_CHECK(writer->close().has_value());
    NX_CHECK(!writer->manifest().segments.empty());
    const std::string source =
        root + "/sess/" + writer->manifest().segments.front().rec_path;
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
                                std::span<const std::byte> /*payload*/,
                                std::uint64_t /*offset*/) {
            ++static_cast<std::uint64_t*>(context)[0];
            return true;
        };
        NX_CHECK(reader->for_each(*channel, true, 0, 0, false, visitor, &visited).has_value());
        NX_CHECK(visited == 3);
        // No `.nxidx` exists next to the copied segment; a ranged query rebuilds it.
        std::uint64_t ranged = 0;
        // Recovered bounds use the commit time for records without a documented
        // event field, so a full widening window is the portable check.
        NX_CHECK(reader->for_each(*channel, true, 0, UINT64_MAX, true, visitor, &ranged)
                     .has_value());
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
    const auto channel = writer->declare_channel("test/shard-gap", 1,
                                                 nexigen::recorder::DatasetKind::Telemetry,
                                                 "nexigen.pod");
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
        NX_CHECK(state.reason == static_cast<std::uint16_t>(
                                    nexigen::recorder::GapReason::IoBackpressure));
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
    nexigen::recorder::VideoGapReason reason =
        nexigen::recorder::GapReason::IoBackpressure;
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
    test_torn_every_byte();
    test_shard();
    test_payload_decoder();
    test_opaque_channel_decode();
    test_publisher_epochs();
    test_segment_only_open();
    test_shard_overflow_gap();
    test_export_selection_roundtrip();
    test_time_sync_record();
    std::printf("recorder tests: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
