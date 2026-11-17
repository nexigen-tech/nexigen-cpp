// nexigen storage-format benchmark — timing, CPU, RSS, metadata, TSV output.
#include "metrics.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <format>
#include <thread>

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#define NEXIGEN_BENCH_HAS_RDTSC 1
#else
#define NEXIGEN_BENCH_HAS_RDTSC 0
#endif

namespace nexigen::bench {

namespace {

[[nodiscard]] std::uint64_t steady_now_ns() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

[[nodiscard]] std::string read_first_line(const char* path) {
    std::ifstream input(path);
    std::string line;
    if (!std::getline(input, line)) {
        return {};
    }
    return line;
}

[[nodiscard]] std::string read_cpu_model() {
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    while (std::getline(input, line)) {
        if (line.starts_with("model name")) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::size_t start = colon + 1;
                while (start < line.size() && line[start] == ' ') {
                    ++start;
                }
                return line.substr(start);
            }
        }
    }
    return "unknown";
}

[[nodiscard]] std::uint64_t read_ram_kb() {
    std::ifstream input("/proc/meminfo");
    std::string key;
    std::uint64_t value = 0;
    std::string unit;
    while (input >> key >> value >> unit) {
        if (key == "MemTotal:") {
            return value;
        }
    }
    return 0;
}

[[nodiscard]] std::string filesystem_name(const std::string& path) {
    struct statfs info {};
    if (::statfs(path.c_str(), &info) != 0) {
        return "unknown";
    }
    switch (static_cast<unsigned long>(info.f_type)) {
        case 0xEF53UL:
            return "ext4";
        case 0x01021994UL:
            return "tmpfs";
        case 0x58465342UL:
            return "xfs";
        case 0x9123683EUL:
            return "btrfs";
        case 0x6969UL:
            return "nfs";
        case 0x794C7630UL:
            return "overlayfs";
        default:
            break;
    }
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "0x%lx", static_cast<unsigned long>(info.f_type));
    return buffer;
}

[[nodiscard]] std::string kernel_name() {
    struct utsname info {};
    if (::uname(&info) != 0) {
        return "unknown";
    }
    return std::string(info.sysname) + " " + info.release;
}

[[nodiscard]] std::string build_type() {
#if defined(NDEBUG) && defined(__OPTIMIZE__)
    return "Release";
#elif defined(__OPTIMIZE__)
    return "RelWithDebInfo";
#else
    return "Debug";
#endif
}

} // namespace

TscClock TscClock::calibrate() {
    TscClock clock;
#if NEXIGEN_BENCH_HAS_RDTSC
    const std::uint64_t t0 = __rdtsc();
    const std::uint64_t m0 = steady_now_ns();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const std::uint64_t m1 = steady_now_ns();
    const std::uint64_t t1 = __rdtsc();
    if (t1 > t0) {
        clock.ns_per_tick_ = static_cast<double>(m1 - m0) / static_cast<double>(t1 - t0);
    }
#else
    clock.ns_per_tick_ = 1.0;
#endif
    return clock;
}

std::uint64_t TscClock::now() const noexcept {
#if NEXIGEN_BENCH_HAS_RDTSC
    return __rdtsc();
#else
    return steady_now_ns();
#endif
}

double measure_tick_overhead(const TscClock& clock) noexcept {
#if NEXIGEN_BENCH_HAS_RDTSC
    constexpr std::size_t kSamples = 1024;
    std::uint64_t samples[kSamples]{};
    for (std::size_t i = 0; i < kSamples; ++i) {
        const std::uint64_t t0 = __rdtsc();
        const std::uint64_t t1 = __rdtsc();
        samples[i] = t1 - t0;
    }
    std::sort(std::begin(samples), std::end(samples));
    return static_cast<double>(samples[kSamples / 2]) * clock.ns_per_tick();
#else
    static_cast<void>(clock);
    return 0.0;
#endif
}

LatencyStats compute_stats(std::vector<std::uint64_t>& ticks, const TscClock& clock,
                           double overhead_ns) {
    LatencyStats stats{};
    if (ticks.empty()) {
        return stats;
    }
    std::sort(ticks.begin(), ticks.end());
    const auto at = [&](std::size_t index) {
        const double ns = static_cast<double>(ticks[index]) * clock.ns_per_tick() - overhead_ns;
        return ns > 0.0 ? ns : 0.0;
    };
    const std::size_t last = ticks.size() - 1;
    stats.p50_ns = at(ticks.size() / 2);
    stats.p99_ns = at(static_cast<std::size_t>(static_cast<double>(ticks.size()) * 0.99));
    stats.p999_ns = at(static_cast<std::size_t>(static_cast<double>(ticks.size()) * 0.999));
    stats.max_ns = at(last);
    double sum = 0.0;
    for (const std::uint64_t tick : ticks) {
        const double ns = static_cast<double>(tick) * clock.ns_per_tick() - overhead_ns;
        sum += ns > 0.0 ? ns : 0.0;
    }
    stats.mean_ns = sum / static_cast<double>(ticks.size());
    return stats;
}

std::uint64_t thread_cpu_ns() noexcept {
    struct timespec value {};
    if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(value.tv_nsec);
}

std::uint64_t process_cpu_ns() noexcept {
    struct timespec value {};
    if (::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(value.tv_nsec);
}

std::uint64_t peak_rss_kb() noexcept {
    struct rusage usage {};
    if (::getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(usage.ru_maxrss);
}

RunMetadata collect_metadata(const std::string& out_directory) {
    RunMetadata metadata{};
    metadata.cpu_model = read_cpu_model();
    metadata.governor =
        read_first_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    if (metadata.governor.empty()) {
        metadata.governor = "unknown";
    }
    metadata.kernel = kernel_name();
    metadata.filesystem = filesystem_name(out_directory);
    metadata.compiler = __VERSION__;
    metadata.build_type = build_type();
#ifdef NEXIGEN_MCAP_COMMIT
    metadata.mcap_commit = NEXIGEN_MCAP_COMMIT;
#else
    metadata.mcap_commit = "unknown";
#endif
    metadata.ram_kb = read_ram_kb();
    return metadata;
}

std::string tsv_header() {
    return "iteration\tworkload\tmode\tsink\tmessages\tbytes\telapsed_ns\tthroughput_mb_s\t"
           "cpu_thread_ms\tcpu_process_ms\tp50_ns\tp99_ns\tp999_ns\tmax_ns\tpeak_rss_kb\t"
           "file_size\tindex_size\trecovery_ns\tcrc_mode\tallocation_free\tallocation_note\t"
           "verified";
}

std::string tsv_row(const RunResult& result) {
    return std::format(
        "{}\t{}\t{}\t{}\t{}\t{}\t{}\t{:.3f}\t{:.3f}\t{:.3f}\t{:.1f}\t{:.1f}\t{:.1f}\t{:.1f}\t"
        "{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
        result.iteration, result.workload, result.mode, result.sink, result.messages,
        result.bytes, result.elapsed_ns, result.throughput_mb_s, result.cpu_thread_ms,
        result.cpu_process_ms, result.latency.p50_ns, result.latency.p99_ns,
        result.latency.p999_ns, result.latency.max_ns, result.peak_rss_kb, result.file_size,
        result.index_size, result.recovery_ns, result.crc_mode,
        result.allocation_free ? 1 : 0, result.allocation_note, result.verified ? 1 : 0);
}

std::string summary_line(const RunResult& result) {
    return std::format(
        "{:<14} {:<22} {:<8} msgs={:<9} {:>8.1f} MB/s  cpu={:>7.1f} ms  "
        "p50={:>7.1f} ns  p99={:>7.1f} ns  rss={:>7} KiB",
        result.workload, result.mode, result.sink, result.messages, result.throughput_mb_s,
        result.cpu_thread_ms, result.latency.p50_ns, result.latency.p99_ns, result.peak_rss_kb);
}

std::string metadata_block(const RunMetadata& metadata, const std::string& command) {
    return std::format(
        "# cpu={}\n# governor={}\n# ram_kb={}\n# kernel={}\n# filesystem={}\n"
        "# compiler={}\n# build_type={}\n# mcap_commit={}\n# command={}\n",
        metadata.cpu_model, metadata.governor, metadata.ram_kb, metadata.kernel,
        metadata.filesystem, metadata.compiler, metadata.build_type, metadata.mcap_commit,
        command);
}

} // namespace nexigen::bench
