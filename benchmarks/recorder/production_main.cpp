// nexigen storage-format benchmark — production writer (nexigen::recorder).
//
// The main nexigen_storage_bench compares storage *formats* with prototype
// writers. This companion target drives the shipped SessionWriter API for the
// two published workloads: telemetry (logs-high) and FullHD 16 bpp 50 Hz video.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <malloc.h>

#include <nexigen/recorder/session_reader.hpp>
#include <nexigen/recorder/session_writer.hpp>
#include <nexigen/recorder/wire_types.hpp>

namespace {

struct Options {
    std::string workload{"logs-high"};
    std::string out_directory{"/var/tmp/nexigen-production-bench"};
    double seconds{1.0};
    bool video_crc{false};
    bool keep{false};
};

struct Stats {
    double p50_ns{0};
    double p99_ns{0};
    std::uint64_t max_ns{0};
};

[[nodiscard]] std::uint64_t steady_ns() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

[[nodiscard]] std::size_t heap_in_use() noexcept {
    return static_cast<std::size_t>(mallinfo2().uordblks);
}

[[nodiscard]] std::uint64_t thread_cpu_ns() noexcept {
    timespec value{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value);
    return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(value.tv_nsec);
}

[[nodiscard]] double percentile_ns(std::vector<std::uint64_t>& values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(
        fraction * static_cast<double>(values.size() - 1U));
    return static_cast<double>(values[index]);
}

[[nodiscard]] Stats compute_stats(std::vector<std::uint64_t>& values) {
    Stats stats{};
    if (values.empty()) {
        return stats;
    }
    auto max = *std::max_element(values.begin(), values.end());
    stats.max_ns = max;
    stats.p50_ns = percentile_ns(values, 0.50);
    stats.p99_ns = percentile_ns(values, 0.99);
    return stats;
}

[[nodiscard]] std::uint64_t directory_bytes(const std::string& directory) noexcept {
    std::error_code error;
    std::uint64_t total = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (entry.is_regular_file(error)) {
            total += entry.file_size(error);
        }
    }
    return total;
}

[[nodiscard]] nexigen::recorder::StorageSettings base_settings(const std::string& root) {
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "bench";
    settings.max_segment_bytes = 1ULL << 30U;
    settings.index_stride_bytes = 256U * 1024U;
    settings.index_interval_ns = 50'000'000U;
    settings.write_buffer_bytes = 1024U * 1024U;
    settings.index_buffer_bytes = 64U * 1024U;
    settings.video_frame_buffer_bytes = 8U * 1024U * 1024U;
    settings.max_record_bytes = 64U * 1024U;
    return settings;
}

[[nodiscard]] bool prepare_root(const std::string& root) {
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return !error;
}

[[nodiscard]] bool verify_session(const std::string& root, std::uint64_t expected) {
    const auto reader = nexigen::recorder::SessionReader::open(root + "/bench");
    if (!reader.has_value()) {
        return false;
    }
    std::uint64_t records = 0;
    std::uint64_t gaps = 0;
    if (!reader->verify(records, gaps).has_value()) {
        return false;
    }
    return records >= expected;
}

void print_result(const Options& options, std::uint64_t messages, std::uint64_t bytes,
                  std::uint64_t elapsed_ns, double cpu_ms, const Stats& stats, bool allocation_free,
                  bool verified) {
    const double mb_s = elapsed_ns == 0
                            ? 0.0
                            : (static_cast<double>(bytes) * 1e9 /
                               static_cast<double>(elapsed_ns)) /
                                  1'000'000.0;
    std::printf("%s production: messages=%llu MB/s=%.1f CPU=%.2f ms "
                "p50=%.0f ns p99=%.0f ns max=%llu ns alloc-free=%s verified=%s\n",
                options.workload.c_str(), static_cast<unsigned long long>(messages), mb_s,
                cpu_ms, stats.p50_ns, stats.p99_ns,
                static_cast<unsigned long long>(stats.max_ns), allocation_free ? "yes" : "no",
                verified ? "yes" : "no");
}

int run_telemetry(const Options& options) {
    const std::string root = options.out_directory + "/production-" + options.workload;
    if (!prepare_root(root)) {
        return 1;
    }
    auto writer = nexigen::recorder::SessionWriter::create(base_settings(root));
    if (!writer.has_value()) {
        std::fprintf(stderr, "cannot create session\n");
        return 1;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "bench.Telemetry";
    schema.version = 1;
    schema.encoding = "jsonschema";
    const std::string json = R"({"type":"object","fields":[]})";
    schema.data.assign(json.begin(), json.end());
    if (!writer->declare_schema(schema).has_value()) {
        std::fprintf(stderr, "cannot declare schema\n");
        return 1;
    }
    const auto channel = writer->declare_channel("bench/telemetry", 1,
                                                 nexigen::recorder::DatasetKind::Telemetry,
                                                 "nexigen.pod");
    if (!channel.has_value()) {
        std::fprintf(stderr, "cannot declare channel\n");
        return 1;
    }

    const std::uint64_t count = static_cast<std::uint64_t>(
        static_cast<double>(100'000) * options.seconds);
    std::vector<std::byte> payload(256, std::byte{0x5A});
    std::vector<std::uint64_t> ticks(count);
    const std::uint64_t heap_before = heap_in_use();
    const std::uint64_t cpu_before = thread_cpu_ns();
    const std::uint64_t wall_start = steady_ns();
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint64_t event_ns = i * 10'000U;
        const std::uint64_t start = steady_ns();
        const auto status = writer->append(nexigen::recorder::RecordType::ComPayload, *channel,
                                           payload, event_ns, i, 1);
        ticks[i] = steady_ns() - start;
        if (!status.has_value()) {
            std::fprintf(stderr, "append failed at %llu\n", static_cast<unsigned long long>(i));
            return 1;
        }
    }
    const std::uint64_t wall_end = steady_ns();
    const std::uint64_t cpu_after = thread_cpu_ns();
    const std::uint64_t heap_after = heap_in_use();
    if (!writer->close().has_value()) {
        std::fprintf(stderr, "close failed\n");
        return 1;
    }
    auto stats = compute_stats(ticks);
    const std::uint64_t bytes = directory_bytes(root + "/bench");
    const bool verified = verify_session(root, count);
    print_result(options, count, bytes, wall_end - wall_start,
                 static_cast<double>(cpu_after - cpu_before) / 1'000'000.0, stats,
                 heap_after == heap_before, verified);
    if (!options.keep) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    return verified ? 0 : 1;
}

int run_video(const Options& options) {
    const std::string root = options.out_directory + "/production-" + options.workload;
    if (!prepare_root(root)) {
        return 1;
    }
    auto settings = base_settings(root);
    settings.video_crc = options.video_crc ? nexigen::recorder::VideoCrcMode::PerFrame
                                           : nexigen::recorder::VideoCrcMode::Off;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    if (!writer.has_value()) {
        std::fprintf(stderr, "cannot create session\n");
        return 1;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "bench.Video";
    schema.version = 1;
    schema.encoding = "jsonschema";
    const std::string json = R"({"type":"object","fields":[]})";
    schema.data.assign(json.begin(), json.end());
    if (!writer->declare_schema(schema).has_value()) {
        return 1;
    }
    const auto channel = writer->declare_channel("bench/camera", 1,
                                                 nexigen::recorder::DatasetKind::Video,
                                                 "nexigen.video");
    if (!channel.has_value()) {
        return 1;
    }
    nexigen::recorder::VideoStreamConfig config{};
    config.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H265);
    config.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
    config.width = 1920;
    config.height = 1080;
    config.config_epoch = 1;

    const std::uint64_t frame_bytes = 1920ULL * 1080ULL * 2ULL;
    const std::uint64_t count =
        static_cast<std::uint64_t>(static_cast<double>(50) * options.seconds);
    std::vector<std::byte> frame(static_cast<std::size_t>(frame_bytes), std::byte{0x11});
    std::vector<std::uint64_t> ticks(count);
    const std::uint64_t heap_before = heap_in_use();
    const std::uint64_t cpu_before = thread_cpu_ns();
    const std::uint64_t wall_start = steady_ns();
    for (std::uint64_t i = 0; i < count; ++i) {
        nexigen::recorder::VideoFrameRef reference{};
        reference.source_sequence = i;
        reference.capture_mono_ns = i * 20'000'000U;
        reference.width = 1920;
        reference.height = 1080;
        reference.codec = config.codec;
        reference.bitstream = config.bitstream;
        const std::uint64_t start = steady_ns();
        const auto status = writer->append_video(*channel, config, reference, frame);
        ticks[i] = steady_ns() - start;
        if (!status.has_value()) {
            std::fprintf(stderr, "append_video failed at %llu\n",
                         static_cast<unsigned long long>(i));
            return 1;
        }
    }
    const std::uint64_t wall_end = steady_ns();
    const std::uint64_t cpu_after = thread_cpu_ns();
    const std::uint64_t heap_after = heap_in_use();
    if (!writer->close().has_value()) {
        std::fprintf(stderr, "close failed\n");
        return 1;
    }
    auto stats = compute_stats(ticks);
    const std::uint64_t bytes = directory_bytes(root + "/bench");
    const bool verified = verify_session(root, count);
    print_result(options, count, bytes, wall_end - wall_start,
                 static_cast<double>(cpu_after - cpu_before) / 1'000'000.0, stats,
                 heap_after == heap_before, verified);
    if (!options.keep) {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    return verified ? 0 : 1;
}

[[nodiscard]] bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--workload" && i + 1 < argc) {
            options.workload = argv[++i];
        } else if (arg == "--seconds" && i + 1 < argc) {
            options.seconds = std::strtod(argv[++i], nullptr);
        } else if (arg == "--out" && i + 1 < argc) {
            options.out_directory = argv[++i];
        } else if (arg == "--video-crc") {
            options.video_crc = true;
        } else if (arg == "--keep") {
            options.keep = true;
        } else {
            std::fprintf(stderr, "usage: %s [--workload logs-high|video-fullhd16-50] "
                                 "[--seconds N] [--out DIR] [--video-crc] [--keep]\n",
                         argv[0]);
            return false;
        }
    }
    return options.workload == "logs-high" || options.workload == "video-fullhd16-50";
}

} // namespace

int main(int argc, char** argv) {
    Options options{};
    if (!parse_options(argc, argv, options)) {
        return 2;
    }
    if (options.workload == "video-fullhd16-50") {
        return run_video(options);
    }
    return run_telemetry(options);
}
