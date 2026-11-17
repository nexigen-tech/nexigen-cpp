// nexigen storage-format benchmark — workload and mode vocabulary.
//
// Benchmark-only types. They are not installed and do not appear in public
// include/nexigen headers, so the project-wide serialization mandates do not
// apply to them.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::bench {

/// @brief One synthetic workload from the benchmark design.
enum class Workload : std::uint8_t {
    LogsLow,
    LogsHigh,
    LogsStress,
    Spans,
    Metrics,
    Video50,
    Video100,
    Video200,
    VideoFullHd16,
};

/// @brief One storage candidate/configuration.
enum class Mode : std::uint8_t {
    McapChunked256k,
    McapChunked1m,
    McapUnchunked,
    McapNoMessageIndex,
    NxrecBatched64k,
    NxrecBatched1m,
    McapVideo,
    NxvidVideo,
    PublishCopy,
    PublishHeader,
};

/// @brief Where the bytes go during a run.
enum class SinkKind : std::uint8_t {
    File,
    Counting,
};

/// @brief Static workload parameters used by every candidate.
struct WorkloadSpec {
    Workload id{Workload::LogsLow};
    std::string_view name{};
    std::uint64_t rate_hz{0};
    std::uint32_t payload_bytes{0};
    bool video{false};
};

/// @brief Static mode parameters used by the runner.
struct ModeSpec {
    Mode id{Mode::McapChunked256k};
    std::string_view name{};
    bool video{false};
    bool publish{false};
};

[[nodiscard]] constexpr std::string_view to_string(Workload workload) noexcept {
    switch (workload) {
        case Workload::LogsLow:
            return "logs-low";
        case Workload::LogsHigh:
            return "logs-high";
        case Workload::LogsStress:
            return "logs-stress";
        case Workload::Spans:
            return "spans";
        case Workload::Metrics:
            return "metrics";
        case Workload::Video50:
            return "video-50";
        case Workload::Video100:
            return "video-100";
        case Workload::Video200:
            return "video-200";
        case Workload::VideoFullHd16:
            return "video-fullhd16-50";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(Mode mode) noexcept {
    switch (mode) {
        case Mode::McapChunked256k:
            return "mcap-chunked-256k";
        case Mode::McapChunked1m:
            return "mcap-chunked-1m";
        case Mode::McapUnchunked:
            return "mcap-unchunked";
        case Mode::McapNoMessageIndex:
            return "mcap-no-message-index";
        case Mode::NxrecBatched64k:
            return "nxrec-batched-64k";
        case Mode::NxrecBatched1m:
            return "nxrec-batched-1m";
        case Mode::McapVideo:
            return "mcap-video";
        case Mode::NxvidVideo:
            return "nxvid-video";
        case Mode::PublishCopy:
            return "publish-copy";
        case Mode::PublishHeader:
            return "publish-header";
    }
    return "unknown";
}

[[nodiscard]] constexpr bool is_video(Workload workload) noexcept {
    return workload == Workload::Video50 || workload == Workload::Video100 ||
           workload == Workload::Video200 || workload == Workload::VideoFullHd16;
}

[[nodiscard]] WorkloadSpec workload_spec(Workload workload) noexcept;

[[nodiscard]] ModeSpec mode_spec(Mode mode) noexcept;

[[nodiscard]] std::optional<Workload> parse_workload(std::string_view text) noexcept;

[[nodiscard]] std::optional<Mode> parse_mode(std::string_view text) noexcept;

/// @brief Parse the benchmark command line.
struct Options {
    Workload workload{Workload::LogsLow};
    Mode mode{Mode::McapChunked256k};
    std::uint32_t iterations{5};
    double seconds{1.0};
    std::string out_directory{"/tmp/nexigen-storage-bench"};
    SinkKind sink{SinkKind::File};
    bool verify{false};
    bool keep_files{false};
    bool video_crc{false};
    bool no_chunk_crc{false};
    bool help{false};
};

[[nodiscard]] core::Result<Options> parse_options(int argc, char** argv) noexcept;

[[nodiscard]] std::string usage_text();

} // namespace nexigen::bench
