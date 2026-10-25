// nexigen::logging — runnable composite logger example.
//
// A CompositeLogger<ConsoleLogger, FileLogger, NullLogger> fans every record
// out to heterogeneous sinks with zero virtual dispatch: one global
// set_level() re-thresholds the whole tree, get<Type>()/get<Index>()
// reconfigure a single sink, several std::jthread producers log through the
// composite, and one unified flush() drains every member. The file output is
// then verified on the cold path. Initialization and flush are checked via
// has_value() / .error() — never value().
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include <nexigen/logging/composite_logger.hpp>
#include <nexigen/logging/console_logger.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/null_logger.hpp>

namespace {

using nexigen::logging::ConsoleLogger;
using nexigen::logging::FileLogger;
using nexigen::logging::LogLevel;
using nexigen::logging::NullLogger;

/// Static storage: the composite owns non-movable shard rings and workers.
nexigen::logging::CompositeLogger<ConsoleLogger<>, FileLogger<>, NullLogger> app{};

constexpr std::string_view k_base_dir = "/tmp/nexigen-examples-logs/composite";
constexpr std::string_view k_file_name = "ex_composite_logger.jsonl";
constexpr std::string_view k_app_name = "nexigen-composite-example";

constexpr std::uint32_t k_workers = 3;
constexpr std::uint32_t k_ticks = 5;
/// The file sink sits at Warning while the composite stays at Info, so only
/// the warning records land in the file: 3 workers x 5 ticks.
constexpr std::uint64_t k_expected_file_records = k_workers * k_ticks;

void print_line(const std::string& line) {
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
}

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

/// Per-sink filtering proof: only warning lines, exactly the expected count.
bool verify_log_file(const std::string& path) {
    std::ifstream in{path};
    if (!in.is_open()) {
        print_line(std::format("cannot open {}", path));
        return false;
    }
    std::uint64_t lines = 0;
    bool ok = true;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        ++lines;
        if (line.find("\"level\":\"warning\"") == std::string::npos) {
            ok = false;
            print_line(std::format("unexpected record in file: {}", line));
        }
        if (line.find("\"level\":\"info\"") != std::string::npos) {
            ok = false;
            print_line("info record leaked into the Warning-level file sink");
        }
        if (line.find(std::format("\"app\":\"{}\"", k_app_name)) == std::string::npos) {
            ok = false;
            print_line(std::format("missing app field: {}", line));
        }
    }
    if (lines != k_expected_file_records) {
        print_line(std::format("record count mismatch: got {}, expected {}", lines,
                               k_expected_file_records));
        return false;
    }
    return ok;
}

} // namespace

int main() {
    // Fresh output tree for the composite sink (the file example owns the
    // parent directory).
    std::error_code cleanup_ec;
    std::filesystem::remove_all(std::string(k_base_dir), cleanup_ec);

    nexigen::logging::FileLogger_config file_config{};
    file_config.app_name = k_app_name;
    file_config.base_dir = k_base_dir;
    file_config.file_name = k_file_name;
    file_config.initial_level = LogLevel::Info;
    file_config.rotation.kind = nexigen::logging::Rotation_schedule::Hourly;
    file_config.core_mask = 0x1;
    file_config.throttle.enabled = true;
    file_config.throttle.max_records_per_window = 4096;
    file_config.throttle.window_ns = 1'000'000'000;

    const auto console_started =
        app.get<ConsoleLogger<>>().initialize(ConsoleLogger<>::Config{.initial_level = LogLevel::Debug});
    if (!console_started.has_value()) {
        print_line(std::format("console initialize failed: {}", console_started.error()));
        return 1;
    }
    const auto file_started = app.get<FileLogger<>>().initialize(file_config);
    if (!file_started.has_value()) {
        print_line(std::format("file initialize failed: {}", file_started.error()));
        return 1;
    }

    // One call re-thresholds the whole tree, NullLogger included.
    app.set_level(LogLevel::Info);
    print_line(std::format("global set_level: composite={}, null={}", app.level(),
                           app.get<NullLogger>().level()));

    // Per-sink policy through the typed accessor; the indexed accessor must
    // address the same stored FileLogger instance (pack order: console=0,
    // file=1, null=2).
    app.get<FileLogger<>>().set_level(LogLevel::Warning);
    const bool same_sink = &app.get<1>() == &app.get<FileLogger<>>();
    print_line(std::format("file sink: level={}, get<1>() addresses get<FileLogger<>>(): {}",
                           app.get<1>().level(), same_sink));

    std::array<std::jthread, k_workers> workers{};
    for (std::uint32_t worker = 0; worker < k_workers; ++worker) {
        workers[worker] = std::jthread{[worker] {
            for (std::uint32_t tick = 0; tick < k_ticks; ++tick) {
                app.info("worker {} info tick {}", worker, tick);
                app.warning("worker {} warning tick {}", worker, tick);
            }
        }};
    }
    for (auto& worker : workers) {
        worker.join();
    }

    // Unified control-plane barrier across all members.
    const auto flushed = app.flush();
    if (!flushed.has_value()) {
        print_line(std::format("flush failed: {}", flushed.error()));
        return 1;
    }

    print_line(std::format("composite: {}", app.to_string()));
    print_line(std::format("console dropped: {}", app.get<ConsoleLogger<>>().dropped().to_string()));
    print_line(std::format("file dropped: {}", app.get<FileLogger<>>().dropped().to_string()));

    const std::string path = find_log_file();
    if (path.empty()) {
        print_line("no composite log file found");
        return 1;
    }
    print_line(std::format("verifying {}", path));
    if (!verify_log_file(path)) {
        return 1;
    }

    print_line("ex_composite_logger: OK");
    return 0;
}
