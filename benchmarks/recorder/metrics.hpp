// nexigen storage-format benchmark — timing, CPU, RSS, metadata, TSV output.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nexigen::bench {

/// @brief Ordered latency statistics in nanoseconds.
struct LatencyStats {
    double p50_ns{0.0};
    double p99_ns{0.0};
    double p999_ns{0.0};
    double max_ns{0.0};
    double mean_ns{0.0};
};

/// @brief TSC calibration and conversion.
class TscClock {
public:
    [[nodiscard]] static TscClock calibrate();

    [[nodiscard]] std::uint64_t now() const noexcept;
    [[nodiscard]] double to_ns(std::uint64_t ticks) const noexcept {
        return static_cast<double>(ticks) * ns_per_tick_;
    }
    [[nodiscard]] double ns_per_tick() const noexcept { return ns_per_tick_; }

private:
    double ns_per_tick_{1.0};
};

/// @brief Median empty-rdtsc-pair overhead in nanoseconds.
[[nodiscard]] double measure_tick_overhead(const TscClock& clock) noexcept;

/// @brief Compute p50/p99/p99.9/max/mean after subtracting the measurement overhead.
[[nodiscard]] LatencyStats compute_stats(std::vector<std::uint64_t>& ticks, const TscClock& clock,
                                         double overhead_ns);

[[nodiscard]] std::uint64_t thread_cpu_ns() noexcept;
[[nodiscard]] std::uint64_t process_cpu_ns() noexcept;
[[nodiscard]] std::uint64_t peak_rss_kb() noexcept;

/// @brief Machine and build metadata recorded with every report.
struct RunMetadata {
    std::string cpu_model{};
    std::string governor{};
    std::string kernel{};
    std::string filesystem{};
    std::string compiler{};
    std::string build_type{};
    std::string mcap_commit{};
    std::uint64_t ram_kb{0};
};

[[nodiscard]] RunMetadata collect_metadata(const std::string& out_directory);

/// @brief One measured run.
struct RunResult {
    std::uint32_t iteration{0};
    std::string workload{};
    std::string mode{};
    std::string sink{};
    std::uint64_t messages{0};
    std::uint64_t bytes{0};
    std::uint64_t elapsed_ns{0};
    double throughput_mb_s{0.0};
    double cpu_thread_ms{0.0};
    double cpu_process_ms{0.0};
    LatencyStats latency{};
    std::uint64_t peak_rss_kb{0};
    std::uint64_t file_size{0};
    std::uint64_t index_size{0};
    std::uint64_t recovery_ns{0};
    std::string crc_mode{};
    bool allocation_free{false};
    std::string allocation_note{};
    bool verified{false};
};

/// @brief TSV header matching `RunResult` field order.
[[nodiscard]] std::string tsv_header();

/// @brief One TSV row.
[[nodiscard]] std::string tsv_row(const RunResult& result);

/// @brief Human-readable one-line summary.
[[nodiscard]] std::string summary_line(const RunResult& result);

/// @brief Metadata block prefixed with `#`.
[[nodiscard]] std::string metadata_block(const RunMetadata& metadata, const std::string& command);

} // namespace nexigen::bench
