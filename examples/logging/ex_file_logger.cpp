// nexigen::logging — runnable asynchronous JSON-lines file logger example.
//
// A default FileLogger is configured end to end (app name, base directory,
// file name, hourly rotation, CPU core mask, throttle budget), accepts
// records from several std::jthread producers — including a UTF-8 Cyrillic
// deferred-format message — flushes, and then verifies its own output file
// on the cold path. Initialization and flush are checked via has_value()
// / .error() — never value().
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/log_level.hpp>

namespace {

/// Static storage: FileLogger owns non-movable shard rings and a worker.
nexigen::logging::FileLogger<> logger{};

constexpr std::string_view k_base_dir = "/tmp/nexigen-examples-logs";
constexpr std::string_view k_file_name = "ex_file_logger.jsonl";
constexpr std::string_view k_app_name = "nexigen-example";

constexpr std::uint32_t k_workers = 3;
constexpr std::uint32_t k_ticks = 5;
/// 3 workers x 5 ticks x (info+warning+error) + 1 Cyrillic debug record.
constexpr std::uint64_t k_expected_records = k_workers * k_ticks * 3 + 1;

void print_line(const std::string& line) {
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
}

bool wait_for_writer() {
    for (std::uint32_t tries = 0; tries < 2000 && !logger.writer_started(); ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return logger.writer_started();
}

/// Locate "<base_dir>/<hourly-label>/<file_name>" without assuming the
/// current hour; rotation labels are UTC-derived.
std::string find_log_file() {
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(std::string(k_base_dir), ec)) {
        if (ec) {
            break;
        }
        const auto candidate = entry.path() / std::string(k_file_name);
        std::error_code exists_ec;
        if (std::filesystem::exists(candidate, exists_ec) && !exists_ec) {
            return candidate.string();
        }
    }
    return {};
}

/// Cold-path verification: schema fields, exact record count, UTF-8 payload.
bool verify_log_file(const std::string& path) {
    std::ifstream in{path};
    if (!in.is_open()) {
        print_line(std::format("cannot open {}", path));
        return false;
    }
    std::uint64_t lines = 0;
    bool cyrillic_seen = false;
    bool schema_ok = true;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        ++lines;
        const bool fields_ok =
            line.find("\"timestamp\":\"") != std::string::npos &&
            line.find("\"level\":\"") != std::string::npos &&
            line.find(std::format("\"app\":\"{}\"", k_app_name)) != std::string::npos &&
            line.find("\"thread_id\":") != std::string::npos &&
            line.find("\"message\":\"") != std::string::npos;
        if (!fields_ok) {
            schema_ok = false;
            print_line(std::format("malformed line: {}", line));
        }
        if (line.find("Прямой вызов метода debug. Давление: 0.0025") != std::string::npos) {
            cyrillic_seen = true;
        }
    }
    if (lines != k_expected_records) {
        print_line(std::format("record count mismatch: got {}, expected {}", lines,
                               k_expected_records));
        return false;
    }
    if (!schema_ok) {
        print_line("schema verification failed");
        return false;
    }
    if (!cyrillic_seen) {
        print_line("UTF-8 Cyrillic message not found in the file");
        return false;
    }
    return true;
}

} // namespace

int main() {
    using nexigen::logging::LogLevel;

    // Fresh output tree: stale runs must not leak into verification.
    std::error_code cleanup_ec;
    std::filesystem::remove_all(std::string(k_base_dir), cleanup_ec);

    nexigen::logging::FileLogger_config config{};
    config.app_name = k_app_name;
    config.base_dir = k_base_dir;
    config.file_name = k_file_name;
    config.initial_level = LogLevel::Debug;
    config.rotation.kind = nexigen::logging::Rotation_schedule::Hourly;
    config.core_mask = 0x1; // request core 0 for the background JSON writer
    config.throttle.enabled = true;
    config.throttle.max_records_per_window = 4096;
    config.throttle.window_ns = 1'000'000'000; // 1 s window

    print_line(std::format("config: {}", config.to_string()));

    const auto started = logger.initialize(config);
    if (!started.has_value()) {
        print_line(std::format("initialize failed: {}", started.error()));
        return 1;
    }

    if (!wait_for_writer()) {
        print_line("writer never reported started");
        return 1;
    }
    // Pinning problems are reported, not hidden; core 0 may be outside the
    // cpuset of a container, so a pin failure alone is not a failure here.
    print_line(std::format("writer: start_code={}, mask=0x{:x}",
                           logger.writer_start_code(), logger.writer_mask()));

    std::array<std::jthread, k_workers> workers{};
    for (std::uint32_t worker = 0; worker < k_workers; ++worker) {
        workers[worker] = std::jthread{[worker] {
            if (worker == 0) {
                logger.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025);
            }
            for (std::uint32_t tick = 0; tick < k_ticks; ++tick) {
                logger.info("worker {} info tick {}", worker, tick);
                logger.warning("worker {} warning tick {}", worker, tick);
                logger.error("worker {} error tick {}", worker, tick);
            }
        }};
    }
    for (auto& worker : workers) {
        worker.join();
    }

    // Control plane: drain all shards, re-sample the UTC offset, fsync.
    const auto flushed = logger.flush();
    if (!flushed.has_value()) {
        print_line(std::format("flush failed: {}", flushed.error()));
        return 1;
    }

    print_line(std::format("dropped: {}", logger.dropped().to_string()));
    print_line(std::format("logger: {}", logger.to_string()));

    const std::string path = find_log_file();
    if (path.empty()) {
        print_line("no log file found under the configured base directory");
        return 1;
    }
    print_line(std::format("verifying {}", path));
    if (!verify_log_file(path)) {
        return 1;
    }

    print_line("ex_file_logger: OK");
    return 0;
}
