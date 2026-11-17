// nexigen storage-format benchmark — unit tests.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>

#include "custom_writer.hpp"
#include "mcap_adapter.hpp"
#include "metrics.hpp"
#include "payload_blob.hpp"
#include "sink.hpp"
#include "video_writer.hpp"

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

[[nodiscard]] std::string test_directory() {
    return "/tmp/nexigen-storage-bench-tests-" + std::to_string(::getpid());
}

void test_payload_blob() {
    const auto blob = nexigen::bench::PayloadBlob::create();
    const auto again = nexigen::bench::PayloadBlob::create();
    NX_CHECK(blob.bytes().size() == nexigen::bench::PayloadBlob::kSize);
    NX_CHECK(blob.checksum() == again.checksum());
    NX_CHECK(blob.checksum() == 0x635CFAB3U); // pinned; update only with a deliberate blob change
    NX_CHECK(nexigen::bench::window_offset(0) == 0);
    const auto window = blob.window(17, 256);
    NX_CHECK(window.size() == 256);
    NX_CHECK(window.data() == blob.bytes().data() + nexigen::bench::window_offset(17));
    std::printf("blob checksum: 0x%08X\n", blob.checksum());
}

void test_sinks() {
    nexigen::bench::CountingSink counting;
    const std::byte data[16]{};
    counting.write(data, sizeof(data));
    NX_CHECK(counting.bytes_written() == sizeof(data));

    const std::string path = test_directory() + "/sink.bin";
    nexigen::bench::FileSink file;
    NX_CHECK(file.open(path));
    file.write(data, sizeof(data));
    file.flush();
    NX_CHECK(file.bytes_written() == sizeof(data));
    NX_CHECK(file.file_size() == sizeof(data));
    file.close();
    std::error_code error;
    NX_CHECK(std::filesystem::file_size(path, error) == sizeof(data));
    std::filesystem::remove(path, error);
}

void test_custom_roundtrip() {
    const std::string rec_path = test_directory() + "/custom.nxrec";
    const std::string idx_path = test_directory() + "/custom.nxidx";
    nexigen::bench::CountingSink rec_count;
    nexigen::bench::CountingSink idx_count;
    nexigen::bench::CustomRecWriter counting(rec_count, idx_count, 64 * 1024,
                                             nexigen::bench::kIndexStride,
                                             nexigen::bench::kIndexIntervalNs);
    const auto blob = nexigen::bench::PayloadBlob::create();
    for (std::uint64_t i = 0; i < 1000; ++i) {
        const auto payload = blob.window(i, 256);
        counting.write(payload.data(), payload.size(), i, i * 1000, i * 1000);
    }
    counting.finish();
    NX_CHECK(counting.record_count() == 1000);
    NX_CHECK(counting.index_count() > 0);
    NX_CHECK(!counting.failed());

    nexigen::bench::FileSink rec_file;
    nexigen::bench::FileSink idx_file;
    NX_CHECK(rec_file.open(rec_path));
    NX_CHECK(idx_file.open(idx_path));
    nexigen::bench::CustomRecWriter writer(rec_file, idx_file, 64 * 1024,
                                           nexigen::bench::kIndexStride,
                                           nexigen::bench::kIndexIntervalNs);
    for (std::uint64_t i = 0; i < 1000; ++i) {
        const auto payload = blob.window(i, 256);
        writer.write(payload.data(), payload.size(), i, i * 1000, i * 1000);
    }
    writer.finish();
    rec_file.close();
    idx_file.close();
    NX_CHECK(writer.record_count() == 1000);

    const auto verify = nexigen::bench::verify_custom_rec(rec_path, idx_path);
    NX_CHECK(verify.has_value());
    if (verify.has_value()) {
        NX_CHECK(verify->messages >= 1000);
        NX_CHECK(verify->header_crc_ok);
        NX_CHECK(verify->payload_crc_ok);
        NX_CHECK(verify->index_ok);
    }
    std::error_code error;
    std::filesystem::remove(rec_path, error);
    std::filesystem::remove(idx_path, error);
}

void test_torn_tail_recovery() {
    const std::string path = test_directory() + "/torn.nxrec";
    nexigen::bench::FileSink file;
    NX_CHECK(file.open(path));
    nexigen::bench::CountingSink index;
    nexigen::bench::CustomRecWriter writer(file, index, 4096, 4096, 0);
    const auto blob = nexigen::bench::PayloadBlob::create();
    for (std::uint64_t i = 0; i < 50; ++i) {
        const auto payload = blob.window(i, 256);
        writer.write(payload.data(), payload.size(), i, i, i);
    }
    writer.finish();
    file.close();

    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    NX_CHECK(!error);
    std::filesystem::resize_file(path, size - 100, error);
    NX_CHECK(!error);

    const auto recovered = nexigen::bench::recover_custom_rec(path);
    NX_CHECK(recovered.has_value());
    if (recovered.has_value()) {
        NX_CHECK(recovered->messages <= 50);
        NX_CHECK(recovered->header_crc_ok);
        NX_CHECK(recovered->payload_crc_ok);
    }
    std::filesystem::remove(path, error);
}

void test_video_alignment() {
    nexigen::bench::CountingSink sink;
    nexigen::bench::VideoWriter writer(sink);
    const auto blob = nexigen::bench::PayloadBlob::create();
    for (std::uint64_t i = 0; i < 10; ++i) {
        const auto frame = blob.window(i, 128 * 1024);
        writer.write_frame(frame.data(), frame.size(), false);
    }
    writer.finish();
    NX_CHECK(writer.frame_count() == 10);
    bool aligned = true;
    for (const auto offset : writer.offsets()) {
        aligned = aligned && offset % nexigen::bench::VideoWriter::kAlignment == 0;
    }
    NX_CHECK(aligned);
    NX_CHECK(sink.bytes_written() >= 10 * 128 * 1024);
}

void test_mcap_roundtrip() {
    const auto blob = nexigen::bench::PayloadBlob::create();
    for (const bool chunked : {true, false}) {
        const std::string path = test_directory() +
                                 (chunked ? "/mcap-chunked.mcap" : "/mcap-unchunked.mcap");
        nexigen::bench::FileSink file;
        NX_CHECK(file.open(path));
        nexigen::bench::McapConfig config{};
        config.chunked = chunked;
        config.chunk_size = 256 * 1024;
        config.no_message_index = !chunked;
        config.no_summary = false;
        config.chunk_crc = chunked;
        config.data_crc = false;
        auto writer = nexigen::bench::McapBenchWriter::create(file, config);
        NX_CHECK(writer.has_value());
        if (!writer.has_value()) {
            continue;
        }
        for (std::uint64_t i = 0; i < 100; ++i) {
            const auto payload = blob.window(i, 256);
            NX_CHECK((*writer)->write(payload.data(), payload.size(),
                                      static_cast<std::uint32_t>(i), i * 1000, i * 1000));
        }
        NX_CHECK((*writer)->close());
        file.close();
        const auto verify = nexigen::bench::verify_mcap(path);
        NX_CHECK(verify.has_value());
        if (verify.has_value()) {
            NX_CHECK(verify->messages == 100);
            NX_CHECK(verify->channels == 1);
        }

        std::error_code error;
        if (!chunked) {
            const std::string truncated = path + ".trunc";
            NX_CHECK(std::filesystem::copy_file(
                path, truncated, std::filesystem::copy_options::overwrite_existing, error));
            const auto full_size = std::filesystem::file_size(truncated, error);
            NX_CHECK(!error);
            std::filesystem::resize_file(truncated, full_size * 4 / 5, error);
            NX_CHECK(!error);
            const auto recovered = nexigen::bench::recover_mcap(truncated);
            NX_CHECK(recovered.has_value());
            if (recovered.has_value()) {
                NX_CHECK(recovered->messages <= 100);
                NX_CHECK(recovered->messages > 0);
            }
            std::filesystem::remove(truncated, error);
        }
        std::filesystem::remove(path, error);
    }
}

void test_metrics() {
    nexigen::bench::TscClock clock = nexigen::bench::TscClock::calibrate();
    NX_CHECK(clock.ns_per_tick() > 0.0);
    std::vector<std::uint64_t> ticks(100);
    for (std::size_t i = 0; i < ticks.size(); ++i) {
        ticks[i] = i + 1;
    }
    const auto stats = nexigen::bench::compute_stats(ticks, clock, 0.0);
    NX_CHECK(stats.p50_ns > 0.0);
    NX_CHECK(stats.p99_ns >= stats.p50_ns);
    NX_CHECK(stats.max_ns >= stats.p99_ns);
    NX_CHECK(nexigen::bench::thread_cpu_ns() > 0);
    NX_CHECK(nexigen::bench::process_cpu_ns() > 0);
    NX_CHECK(nexigen::bench::peak_rss_kb() > 0);
}

} // namespace

int main() {
    std::error_code error;
    std::filesystem::create_directories(test_directory(), error);
    test_payload_blob();
    test_sinks();
    test_custom_roundtrip();
    test_torn_tail_recovery();
    test_video_alignment();
    test_mcap_roundtrip();
    test_metrics();
    std::filesystem::remove_all(test_directory(), error);
    std::printf("storage-bench tests: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
