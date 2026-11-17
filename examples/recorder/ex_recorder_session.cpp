// ex_recorder_session — write a tiny raw session and read it back.
//
// The example uses only the public nexigen::recorder API: no COM, no MCAP, no
// media backend. It demonstrates the producer path (settings -> schemas ->
// channels -> append -> close) and the cold path (open -> datasets -> verify).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <nexigen/recorder/builtin_schemas.hpp>
#include <nexigen/recorder/session_reader.hpp>
#include <nexigen/recorder/session_writer.hpp>

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "nexigen-example-recorder";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root.string();
    settings.session_id = "example";
    settings.max_segment_bytes = 1U << 20U;
    settings.index_stride_bytes = 64U * 1024U;
    settings.index_interval_ns = 50'000'000U;
    settings.write_buffer_bytes = 8192;
    settings.index_buffer_bytes = 4096;
    settings.video_frame_buffer_bytes = 1U << 20U;
    settings.max_record_bytes = 1U << 16U;

    auto writer = nexigen::recorder::SessionWriter::create(settings);
    if (!writer.has_value()) {
        std::fprintf(stderr, "cannot create session\n");
        return 1;
    }
    for (const auto& schema : nexigen::recorder::builtin_schemas()) {
        if (!writer->declare_schema(schema).has_value()) {
            std::fprintf(stderr, "cannot declare schema %s\n", schema.name.c_str());
            return 1;
        }
    }
    const auto channel = writer->declare_channel(
        "example/telemetry", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    if (!channel.has_value()) {
        std::fprintf(stderr, "cannot declare channel\n");
        return 1;
    }

    // A real producer would hand over its frame bytes here; the recorder treats
    // the payload as opaque bytes and covers it with CRC-32C.
    const std::vector<std::byte> payload(64, std::byte{0x2A});
    for (std::uint64_t i = 0; i < 3; ++i) {
        const auto status = writer->append(nexigen::recorder::RecordType::LogFrame, *channel,
                                           payload, i * 1'000'000U, i, 1);
        if (!status.has_value()) {
            std::fprintf(stderr, "cannot append record %llu\n",
                         static_cast<unsigned long long>(i));
            return 1;
        }
    }
    if (!writer->close().has_value()) {
        std::fprintf(stderr, "cannot close session\n");
        return 1;
    }

    auto reader = nexigen::recorder::SessionReader::open(root / "example");
    if (!reader.has_value()) {
        std::fprintf(stderr, "cannot reopen session\n");
        return 1;
    }
    const auto datasets = reader->datasets();
    if (!datasets.has_value()) {
        std::fprintf(stderr, "cannot build inventory\n");
        return 1;
    }
    std::uint64_t records = 0;
    std::uint64_t gaps = 0;
    const auto verified = reader->verify(records, gaps);
    if (!verified.has_value()) {
        std::fprintf(stderr, "verification failed\n");
        return 1;
    }
    std::printf("session=%s datasets=%zu records=%llu gaps=%llu\n",
                reader->directory().c_str(), datasets->size(),
                static_cast<unsigned long long>(records),
                static_cast<unsigned long long>(gaps));
    for (const auto& dataset : *datasets) {
        std::printf("  channel %u %s records=%llu bytes=%llu\n", dataset.channel_id,
                    dataset.name.c_str(), static_cast<unsigned long long>(dataset.records),
                    static_cast<unsigned long long>(dataset.payload_bytes));
    }

    fs::remove_all(root, error);
    return 0;
}
