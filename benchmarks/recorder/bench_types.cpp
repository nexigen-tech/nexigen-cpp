// nexigen storage-format benchmark — workload/mode tables and CLI parsing.
#include "bench_types.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

namespace nexigen::bench {

namespace {

template <class T>
[[nodiscard]] bool parse_number(std::string_view text, T& out) noexcept {
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] bool video_mode(Mode mode) noexcept {
    return mode == Mode::McapVideo || mode == Mode::NxvidVideo;
}

[[nodiscard]] bool publish_mode(Mode mode) noexcept {
    return mode == Mode::PublishCopy || mode == Mode::PublishHeader;
}

} // namespace

WorkloadSpec workload_spec(Workload workload) noexcept {
    switch (workload) {
        case Workload::LogsLow:
            return {workload, "logs-low", 10'000, 256, false};
        case Workload::LogsHigh:
            return {workload, "logs-high", 100'000, 256, false};
        case Workload::LogsStress:
            return {workload, "logs-stress", 1'000'000, 256, false};
        case Workload::Spans:
            return {workload, "spans", 100'000, 384, false};
        case Workload::Metrics:
            return {workload, "metrics", 1'000, 4'464, false};
        case Workload::Video50:
            return {workload, "video-50", 0, 128 * 1024, true};
        case Workload::Video100:
            return {workload, "video-100", 0, 128 * 1024, true};
        case Workload::Video200:
            return {workload, "video-200", 0, 128 * 1024, true};
        case Workload::VideoFullHd16:
            return {workload, "video-fullhd16-50", 50, 1920 * 1080 * 2, true};
    }
    return {};
}

ModeSpec mode_spec(Mode mode) noexcept {
    switch (mode) {
        case Mode::McapChunked256k:
            return {mode, "mcap-chunked-256k", false, false};
        case Mode::McapChunked1m:
            return {mode, "mcap-chunked-1m", false, false};
        case Mode::McapUnchunked:
            return {mode, "mcap-unchunked", false, false};
        case Mode::McapNoMessageIndex:
            return {mode, "mcap-no-message-index", false, false};
        case Mode::NxrecBatched64k:
            return {mode, "nxrec-batched-64k", false, false};
        case Mode::NxrecBatched1m:
            return {mode, "nxrec-batched-1m", false, false};
        case Mode::McapVideo:
            return {mode, "mcap-video", true, false};
        case Mode::NxvidVideo:
            return {mode, "nxvid-video", true, false};
        case Mode::PublishCopy:
            return {mode, "publish-copy", false, true};
        case Mode::PublishHeader:
            return {mode, "publish-header", false, true};
    }
    return {};
}

std::optional<Workload> parse_workload(std::string_view text) noexcept {
    constexpr Workload all[] = {Workload::LogsLow,  Workload::LogsHigh, Workload::LogsStress,
                                Workload::Spans,    Workload::Metrics,  Workload::Video50,
                                Workload::Video100, Workload::Video200, Workload::VideoFullHd16};
    for (const Workload workload : all) {
        if (to_string(workload) == text) {
            return workload;
        }
    }
    return std::nullopt;
}

std::optional<Mode> parse_mode(std::string_view text) noexcept {
    constexpr Mode all[] = {Mode::McapChunked256k, Mode::McapChunked1m,   Mode::McapUnchunked,
                            Mode::McapNoMessageIndex, Mode::NxrecBatched64k, Mode::NxrecBatched1m,
                            Mode::McapVideo,      Mode::NxvidVideo,       Mode::PublishCopy,
                            Mode::PublishHeader};
    for (const Mode mode : all) {
        if (to_string(mode) == text) {
            return mode;
        }
    }
    return std::nullopt;
}

core::Result<Options> parse_options(int argc, char** argv) noexcept {
    Options options{};
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        const auto next = [&]() -> std::string_view {
            if (i + 1 >= argc) {
                return {};
            }
            return std::string_view(argv[++i]);
        };
        if (arg == "--help" || arg == "-h") {
            options.help = true;
            continue;
        }
        if (arg == "--verify") {
            options.verify = true;
            continue;
        }
        if (arg == "--keep") {
            options.keep_files = true;
            continue;
        }
        if (arg == "--video-crc") {
            options.video_crc = true;
            continue;
        }
        if (arg == "--no-chunk-crc") {
            options.no_chunk_crc = true;
            continue;
        }
        if (arg == "--workload") {
            const std::string_view value = next();
            const auto parsed = parse_workload(value);
            if (!parsed.has_value()) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            options.workload = *parsed;
            continue;
        }
        if (arg == "--mode") {
            const std::string_view value = next();
            const auto parsed = parse_mode(value);
            if (!parsed.has_value()) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            options.mode = *parsed;
            continue;
        }
        if (arg == "--iterations") {
            const std::string_view value = next();
            if (!parse_number(value, options.iterations) || options.iterations == 0U) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            continue;
        }
        if (arg == "--seconds") {
            const std::string_view value = next();
            if (!parse_number(value, options.seconds) || !std::isfinite(options.seconds) ||
                options.seconds <= 0.0) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            continue;
        }
        if (arg == "--out") {
            const std::string_view value = next();
            if (value.empty()) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            options.out_directory = std::string(value);
            continue;
        }
        if (arg == "--sink") {
            const std::string_view value = next();
            if (value == "file") {
                options.sink = SinkKind::File;
            } else if (value == "counting") {
                options.sink = SinkKind::Counting;
            } else {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            continue;
        }
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    if (options.help) {
        return options;
    }
    const bool video_workload = is_video(options.workload);
    if (video_mode(options.mode) != video_workload && !publish_mode(options.mode)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (publish_mode(options.mode) && is_video(options.workload)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return options;
}

std::string usage_text() {
    return "usage: nexigen_storage_bench --workload <name> --mode <name> [options]\n"
           "\n"
           "workloads: logs-low logs-high logs-stress spans metrics video-50 video-100 video-200\n"
           "           video-fullhd16-50 (1920x1080, 16 bpp, 50 Hz)\n"
           "modes:     mcap-chunked-256k mcap-chunked-1m mcap-unchunked\n"
           "           mcap-no-message-index nxrec-batched-64k nxrec-batched-1m\n"
           "           mcap-video nxvid-video publish-copy publish-header\n"
           "\n"
           "options:\n"
           "  --iterations N   measured iterations (default 5)\n"
           "  --seconds S      measured seconds per iteration (default 1.0)\n"
           "  --out DIR        output directory (default /tmp/nexigen-storage-bench)\n"
           "  --sink KIND      file | counting (default file)\n"
           "  --verify         reopen/verify the written file after the run\n"
           "  --video-crc      compute a CRC-32C per .nxvid frame (default off)\n"
           "  --no-chunk-crc   disable the MCAP chunk CRC (default on)\n"
           "  --keep           keep output files (default: remove after reporting)\n"
           "  --help, -h       print this help\n";
}

} // namespace nexigen::bench
