// nexigen storage-format benchmark — CLI runner.
#include "bench_types.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <malloc.h>

#include "custom_writer.hpp"
#include "mcap_adapter.hpp"
#include "metrics.hpp"
#include "payload_blob.hpp"
#include "sink.hpp"
#include "video_writer.hpp"

#if defined(NEXIGEN_BENCH_HAS_COM)
#include "publish_bench.hpp"
#endif

namespace nexigen::bench {

namespace {

constexpr std::uint64_t kVideoFrameBytes = 128U * 1024U;

[[nodiscard]] std::uint64_t steady_ns() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

[[nodiscard]] std::size_t heap_in_use() noexcept {
    return static_cast<std::size_t>(mallinfo2().uordblks);
}

[[nodiscard]] std::string path_join(const std::string& directory, const std::string& name) {
    if (!directory.empty() && directory.back() == '/') {
        return directory + name;
    }
    return directory + "/" + name;
}

[[nodiscard]] std::string sink_name(SinkKind sink) noexcept {
    return sink == SinkKind::File ? "file" : "counting";
}

[[nodiscard]] std::string workload_tag(const WorkloadSpec& spec) {
    return std::string(spec.name);
}

[[nodiscard]] std::uint64_t message_count(const WorkloadSpec& spec, double seconds) noexcept {
    if (spec.video) {
        return 0;
    }
    return static_cast<std::uint64_t>(static_cast<double>(spec.rate_hz) * seconds);
}

[[nodiscard]] std::uint64_t video_rate_mb(Workload workload) noexcept {
    switch (workload) {
        case Workload::Video50:
            return 50;
        case Workload::Video100:
            return 100;
        case Workload::Video200:
            return 200;
        default:
            return 0;
    }
}

[[nodiscard]] std::uint64_t video_frames(const WorkloadSpec& spec, double seconds) noexcept {
    if (spec.id == Workload::VideoFullHd16) {
        const std::uint64_t frames =
            static_cast<std::uint64_t>(static_cast<double>(spec.rate_hz) * seconds);
        return frames == 0 ? 1 : frames;
    }
    const std::uint64_t mb_per_second = video_rate_mb(spec.id);
    const std::uint64_t target_bytes =
        static_cast<std::uint64_t>(static_cast<double>(mb_per_second) * 1'000'000.0 * seconds);
    const std::uint64_t frames = target_bytes / kVideoFrameBytes;
    return frames == 0 ? 1 : frames;
}

[[nodiscard]] std::uint64_t video_timestamp(std::uint64_t frame,
                                            const WorkloadSpec& spec) noexcept {
    if (spec.rate_hz > 0) {
        return static_cast<std::uint64_t>(static_cast<double>(frame) * 1e9 /
                                          static_cast<double>(spec.rate_hz));
    }
    return frame * 1'000'000ULL;
}

[[nodiscard]] std::uint32_t low32(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value & 0xFFFFFFFFULL);
}

struct RunOutput {
    RunResult result{};
    bool write_failed{false};
};

RunOutput run_mcap(const Options& options, const WorkloadSpec& spec, std::uint32_t iteration,
                   const PayloadBlob& blob, const TscClock& clock, double overhead_ns) {
    RunOutput output{};
    output.result.iteration = iteration;
    output.result.workload = workload_tag(spec);
    output.result.mode = std::string(mode_spec(options.mode).name);
    output.result.sink = sink_name(options.sink);
    output.result.crc_mode = "chunk-crc32";

    McapConfig config{};
    switch (options.mode) {
        case Mode::McapChunked256k:
            config = {.chunked = true, .chunk_size = 256U * 1024U, .no_message_index = false,
                      .no_summary = false, .chunk_crc = true, .data_crc = false};
            break;
        case Mode::McapChunked1m:
            config = {.chunked = true, .chunk_size = 1024U * 1024U, .no_message_index = false,
                      .no_summary = false, .chunk_crc = true, .data_crc = false};
            break;
        case Mode::McapUnchunked:
            config = {.chunked = false, .chunk_size = 0, .no_message_index = true,
                      .no_summary = false, .chunk_crc = false, .data_crc = false};
            output.result.crc_mode = "none";
            break;
        case Mode::McapNoMessageIndex:
            config = {.chunked = true, .chunk_size = 256U * 1024U, .no_message_index = true,
                      .no_summary = false, .chunk_crc = true, .data_crc = false};
            break;
        case Mode::McapVideo:
            config = {.chunked = true, .chunk_size = 1024U * 1024U, .no_message_index = false,
                      .no_summary = false, .chunk_crc = true, .data_crc = false};
            break;
        default:
            config = {};
            break;
    }
    if (config.chunked) {
        config.chunk_crc = !options.no_chunk_crc;
        output.result.crc_mode = config.chunk_crc ? "chunk-crc32" : "none";
    } else {
        output.result.crc_mode = "none";
    }

    CountingSink counting{};
    FileSink file{};
    Sink* sink = &counting;
    std::string path;
    if (options.sink == SinkKind::File) {
        path = path_join(options.out_directory,
                         "mcap-" + workload_tag(spec) + "-" + std::string(mode_spec(options.mode).name) +
                             "-" + std::to_string(iteration) + ".mcap");
        if (!file.open(path)) {
            output.write_failed = true;
            return output;
        }
        sink = &file;
    }

    auto writer = McapBenchWriter::create(*sink, config);
    if (!writer.has_value()) {
        output.write_failed = true;
        return output;
    }

    const bool video = spec.video;
    const std::uint64_t count = video ? video_frames(spec, options.seconds)
                                      : message_count(spec, options.seconds);
    std::vector<std::uint64_t> ticks(count);
    const std::uint64_t cpu_thread_before = thread_cpu_ns();
    const std::uint64_t cpu_process_before = process_cpu_ns();
    const std::size_t heap_before = heap_in_use();
    const std::uint64_t wall_start = steady_ns();

    for (std::uint64_t i = 0; i < count; ++i) {
        const std::size_t size = static_cast<std::size_t>(spec.payload_bytes);
        const auto payload = blob.window(i, size);
        const std::uint64_t event_ns =
            video ? video_timestamp(i, spec)
                  : static_cast<std::uint64_t>(static_cast<double>(i) * 1e9 /
                                               static_cast<double>(spec.rate_hz));
        const std::uint64_t start = clock.now();
        const bool ok = (*writer)->write(payload.data(), payload.size(), low32(i), event_ns,
                                         event_ns);
        const std::uint64_t end = clock.now();
        ticks[i] = end - start;
        if (!ok) {
            output.write_failed = true;
            break;
        }
    }

    if (!(*writer)->close()) {
        output.write_failed = true;
    }

    const std::uint64_t wall_end = steady_ns();
    const std::uint64_t cpu_thread_after = thread_cpu_ns();
    const std::uint64_t cpu_process_after = process_cpu_ns();
    const std::size_t heap_after = heap_in_use();

    output.result.messages = (*writer)->stats().messages;
    output.result.bytes = (*writer)->stats().bytes;
    output.result.elapsed_ns = wall_end > wall_start ? wall_end - wall_start : 0;
    output.result.throughput_mb_s =
        output.result.elapsed_ns == 0
            ? 0.0
            : (static_cast<double>(output.result.bytes) * 1e9 /
               static_cast<double>(output.result.elapsed_ns)) /
                  1'000'000.0;
    output.result.cpu_thread_ms =
        static_cast<double>(cpu_thread_after - cpu_thread_before) / 1'000'000.0;
    output.result.cpu_process_ms =
        static_cast<double>(cpu_process_after - cpu_process_before) / 1'000'000.0;
    output.result.latency = compute_stats(ticks, clock, overhead_ns);
    output.result.peak_rss_kb = peak_rss_kb();
    output.result.file_size = sink->bytes_written();
    output.result.index_size = 0;
    output.result.allocation_free = heap_after == heap_before;
    output.result.allocation_note =
        output.result.allocation_free ? "none"
                                      : "mcap writer (schema/channel/message-index/statistics)";

    if (options.verify && !path.empty()) {
        const auto verify = verify_mcap(path);
        if (verify.has_value()) {
            output.result.verified =
                verify->messages == output.result.messages && verify->channels >= 1;
        }
    } else {
        output.result.verified = true;
    }

    if (options.sink == SinkKind::File && !options.keep_files && !path.empty()) {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    return output;
}

RunOutput run_custom(const Options& options, const WorkloadSpec& spec, std::uint32_t iteration,
                     const PayloadBlob& blob, const TscClock& clock, double overhead_ns) {
    RunOutput output{};
    output.result.iteration = iteration;
    output.result.workload = workload_tag(spec);
    output.result.mode = std::string(mode_spec(options.mode).name);
    output.result.sink = sink_name(options.sink);
    output.result.crc_mode = "crc32c-per-record";

    CountingSink counting_rec{};
    CountingSink counting_idx{};
    FileSink file_rec{};
    FileSink file_idx{};
    Sink* rec = &counting_rec;
    Sink* idx = &counting_idx;
    std::string rec_path;
    std::string idx_path;
    if (options.sink == SinkKind::File) {
        const std::string base = "nxrec-" + workload_tag(spec) + "-" +
                                 std::string(mode_spec(options.mode).name) + "-" +
                                 std::to_string(iteration);
        rec_path = path_join(options.out_directory, base + ".nxrec");
        idx_path = path_join(options.out_directory, base + ".nxidx");
        if (!file_rec.open(rec_path) || !file_idx.open(idx_path)) {
            output.write_failed = true;
            return output;
        }
        rec = &file_rec;
        idx = &file_idx;
    }

    const std::size_t buffer_size =
        options.mode == Mode::NxrecBatched1m ? 1024U * 1024U : 64U * 1024U;
    CustomRecWriter writer(*rec, *idx, buffer_size, kIndexStride, kIndexIntervalNs);
    const std::uint64_t count = message_count(spec, options.seconds);
    std::vector<std::uint64_t> ticks(count);
    const std::uint64_t cpu_thread_before = thread_cpu_ns();
    const std::uint64_t cpu_process_before = process_cpu_ns();
    const std::size_t heap_before = heap_in_use();
    const std::uint64_t wall_start = steady_ns();

    for (std::uint64_t i = 0; i < count; ++i) {
        const auto payload = blob.window(i, static_cast<std::size_t>(spec.payload_bytes));
        const std::uint64_t event_ns = static_cast<std::uint64_t>(
            static_cast<double>(i) * 1e9 / static_cast<double>(spec.rate_hz));
        const std::uint64_t start = clock.now();
        writer.write(payload.data(), payload.size(), i, event_ns, event_ns);
        const std::uint64_t end = clock.now();
        ticks[i] = end - start;
    }
    writer.finish();

    const std::uint64_t wall_end = steady_ns();
    const std::uint64_t cpu_thread_after = thread_cpu_ns();
    const std::uint64_t cpu_process_after = process_cpu_ns();
    const std::size_t heap_after = heap_in_use();

    output.write_failed = writer.failed();
    output.result.messages = writer.record_count();
    output.result.bytes = rec->bytes_written();
    output.result.elapsed_ns = wall_end > wall_start ? wall_end - wall_start : 0;
    output.result.throughput_mb_s =
        output.result.elapsed_ns == 0
            ? 0.0
            : (static_cast<double>(output.result.bytes) * 1e9 /
               static_cast<double>(output.result.elapsed_ns)) /
                  1'000'000.0;
    output.result.cpu_thread_ms =
        static_cast<double>(cpu_thread_after - cpu_thread_before) / 1'000'000.0;
    output.result.cpu_process_ms =
        static_cast<double>(cpu_process_after - cpu_process_before) / 1'000'000.0;
    output.result.latency = compute_stats(ticks, clock, overhead_ns);
    output.result.peak_rss_kb = peak_rss_kb();
    output.result.file_size = rec->bytes_written();
    output.result.index_size = idx->bytes_written();
    output.result.allocation_free = heap_after == heap_before;
    output.result.allocation_note =
        output.result.allocation_free ? "none" : "custom writer buffer/index";

    if (options.verify && options.sink == SinkKind::File) {
        const auto verify = verify_custom_rec(rec_path, idx_path);
        if (verify.has_value()) {
            output.result.verified = verify->messages == output.result.messages &&
                                     verify->header_crc_ok && verify->payload_crc_ok &&
                                     verify->index_ok;
        }
    } else {
        output.result.verified = true;
    }

    if (options.sink == SinkKind::File && !options.keep_files) {
        std::error_code error;
        std::filesystem::remove(rec_path, error);
        std::filesystem::remove(idx_path, error);
    }
    return output;
}

RunOutput run_nxvid(const Options& options, const WorkloadSpec& spec, std::uint32_t iteration,
                    const PayloadBlob& blob, const TscClock& clock, double overhead_ns) {
    RunOutput output{};
    output.result.iteration = iteration;
    output.result.workload = workload_tag(spec);
    output.result.mode = std::string(mode_spec(options.mode).name);
    output.result.sink = sink_name(options.sink);
    output.result.crc_mode = options.video_crc ? "crc32c-per-frame" : "none";

    CountingSink counting{};
    FileSink file{};
    Sink* sink = &counting;
    std::string path;
    if (options.sink == SinkKind::File) {
        path = path_join(options.out_directory,
                         "nxvid-" + workload_tag(spec) + "-" + std::to_string(iteration) + ".nxvid");
        if (!file.open(path)) {
            output.write_failed = true;
            return output;
        }
        sink = &file;
    }

    VideoWriter writer(*sink);
    const std::uint64_t count = video_frames(spec, options.seconds);
    writer.reserve(static_cast<std::size_t>(count));
    std::vector<std::uint64_t> ticks(count);
    const std::uint64_t cpu_thread_before = thread_cpu_ns();
    const std::uint64_t cpu_process_before = process_cpu_ns();
    const std::size_t heap_before = heap_in_use();
    const std::uint64_t wall_start = steady_ns();

    for (std::uint64_t i = 0; i < count; ++i) {
        const auto payload = blob.window(i, static_cast<std::size_t>(spec.payload_bytes));
        const std::uint64_t start = clock.now();
        writer.write_frame(payload.data(), payload.size(), options.video_crc);
        const std::uint64_t end = clock.now();
        ticks[i] = end - start;
    }
    writer.finish();

    const std::uint64_t wall_end = steady_ns();
    const std::uint64_t cpu_thread_after = thread_cpu_ns();
    const std::uint64_t cpu_process_after = process_cpu_ns();
    const std::size_t heap_after = heap_in_use();

    output.write_failed = writer.failed();
    output.result.messages = writer.frame_count();
    output.result.bytes = sink->bytes_written();
    output.result.elapsed_ns = wall_end > wall_start ? wall_end - wall_start : 0;
    output.result.throughput_mb_s =
        output.result.elapsed_ns == 0
            ? 0.0
            : (static_cast<double>(output.result.bytes) * 1e9 /
               static_cast<double>(output.result.elapsed_ns)) /
                  1'000'000.0;
    output.result.cpu_thread_ms =
        static_cast<double>(cpu_thread_after - cpu_thread_before) / 1'000'000.0;
    output.result.cpu_process_ms =
        static_cast<double>(cpu_process_after - cpu_process_before) / 1'000'000.0;
    output.result.latency = compute_stats(ticks, clock, overhead_ns);
    output.result.peak_rss_kb = peak_rss_kb();
    output.result.file_size = sink->bytes_written();
    output.result.index_size = 0;
    output.result.allocation_free = heap_after == heap_before;
    output.result.allocation_note =
        output.result.allocation_free ? "none" : "video frame bookkeeping";

    bool aligned = true;
    for (const std::uint64_t offset : writer.offsets()) {
        if (offset % VideoWriter::kAlignment != 0U) {
            aligned = false;
            break;
        }
    }
    output.result.verified =
        options.verify
            ? (aligned && writer.frame_count() == count &&
               (!options.video_crc || writer.crcs().size() == count))
            : true;

    if (options.sink == SinkKind::File && !options.keep_files && !path.empty()) {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    return output;
}

RunOutput run_one(const Options& options, const WorkloadSpec& spec, std::uint32_t iteration,
                  const PayloadBlob& blob, const TscClock& clock, double overhead_ns) {
    if (options.mode == Mode::NxvidVideo) {
        return run_nxvid(options, spec, iteration, blob, clock, overhead_ns);
    }
    if (options.mode == Mode::McapVideo) {
        return run_mcap(options, spec, iteration, blob, clock, overhead_ns);
    }
    if (options.mode == Mode::NxrecBatched64k || options.mode == Mode::NxrecBatched1m) {
        return run_custom(options, spec, iteration, blob, clock, overhead_ns);
    }
    return run_mcap(options, spec, iteration, blob, clock, overhead_ns);
}

int run_publish(const Options& options) {
    static_cast<void>(options);
#if defined(NEXIGEN_BENCH_HAS_COM)
    const auto result = run_publish_bench();
    if (!result.has_value()) {
        std::fprintf(stderr, "publish benchmark failed\n");
        return 1;
    }
    std::printf("publish copy:    p50 %.1f ns p99 %.1f ns p99.9 %.1f ns max %.1f ns\n",
                result->send_copy.p50_ns, result->send_copy.p99_ns,
                result->send_copy.p999_ns, result->send_copy.max_ns);
    std::printf("publish header:  p50 %.1f ns p99 %.1f ns p99.9 %.1f ns max %.1f ns\n",
                result->loan_with_header.p50_ns, result->loan_with_header.p99_ns,
                result->loan_with_header.p999_ns, result->loan_with_header.max_ns);
    std::printf("producer push:   p50 %.1f ns p99 %.1f ns p99.9 %.1f ns max %.1f ns\n",
                result->producer_try_push.p50_ns, result->producer_try_push.p99_ns,
                result->producer_try_push.p999_ns, result->producer_try_push.max_ns);
    std::printf("heap delta: %lld B  failures: %s\n",
                static_cast<long long>(result->heap_delta_bytes),
                result->ok ? "none" : "present");
    return result->ok ? 0 : 1;
#else
    static_cast<void>(options);
    std::fprintf(stderr, "publish benchmark unavailable: NEXIGEN_ENABLE_COM is OFF\n");
    return 2;
#endif
}

} // namespace
} // namespace nexigen::bench

int main(int argc, char** argv) {
    using namespace nexigen::bench;
    const auto parsed = parse_options(argc, argv);
    if (!parsed.has_value()) {
        std::fprintf(stderr, "nexigen_storage_bench: invalid arguments\n\n%s",
                     usage_text().c_str());
        return 2;
    }
    const Options& options = *parsed;
    if (options.help) {
        std::printf("%s", usage_text().c_str());
        return 0;
    }
    if (options.mode == Mode::PublishCopy || options.mode == Mode::PublishHeader) {
        return run_publish(options);
    }

    std::error_code error;
    std::filesystem::create_directories(options.out_directory, error);
    if (error) {
        std::fprintf(stderr, "nexigen_storage_bench: cannot create %s\n",
                     options.out_directory.c_str());
        return 1;
    }

    const WorkloadSpec spec = workload_spec(options.workload);
    const PayloadBlob blob = PayloadBlob::create();
    const TscClock clock = TscClock::calibrate();
    const double overhead_ns = measure_tick_overhead(clock);
    const RunMetadata metadata = collect_metadata(options.out_directory);

    std::string command;
    for (int i = 0; i < argc; ++i) {
        if (i != 0) {
            command += ' ';
        }
        command += argv[i];
    }

    const std::string tsv_path = path_join(options.out_directory, "results.tsv");
    std::ofstream tsv(tsv_path, std::ios::app);
    if (!tsv) {
        std::fprintf(stderr, "nexigen_storage_bench: cannot open %s\n", tsv_path.c_str());
        return 1;
    }
    tsv << metadata_block(metadata, command);
    tsv << tsv_header() << '\n';

    std::vector<RunResult> results;
    bool failed = false;
    for (std::uint32_t iteration = 1; iteration <= options.iterations; ++iteration) {
        RunOutput output = run_one(options, spec, iteration, blob, clock, overhead_ns);
        const RunResult& result = output.result;
        std::printf("%s\n", summary_line(result).c_str());
        tsv << tsv_row(result) << '\n';
        tsv.flush();
        results.push_back(result);
        if (output.write_failed) {
            std::fprintf(stderr, "write failed in iteration %u\n", iteration);
            failed = true;
        }
        if (!result.verified) {
            std::fprintf(stderr, "verification failed in iteration %u\n", iteration);
            failed = true;
        }
    }

    if (!results.empty()) {
        std::vector<double> throughput;
        std::vector<double> p99;
        throughput.reserve(results.size());
        p99.reserve(results.size());
        for (const auto& result : results) {
            throughput.push_back(result.throughput_mb_s);
            p99.push_back(result.latency.p99_ns);
        }
        std::sort(throughput.begin(), throughput.end());
        std::sort(p99.begin(), p99.end());
        const std::size_t middle = throughput.size() / 2;
        std::printf("median: %.1f MB/s, p99 %.1f ns over %zu iterations\n",
                    throughput[middle], p99[middle], results.size());
    }
    return failed ? 1 : 0;
}
