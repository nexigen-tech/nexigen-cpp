// nexigen::logging — runnable global/local settings loader example.
//
// Writes a temporary global/local JSON pair, layers environment overrides on
// top, loads the merged `Logging_settings`, applies them to a `FileLogger`,
// logs, flushes, and verifies its own JSON-lines output on the cold path.
// Initialization and flush are checked via has_value() / .error() — never
// value().
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>

#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/logging_settings.hpp>

namespace {

using nexigen::logging::FileLogger;
using nexigen::logging::LogLevel;
using nexigen::logging::Logging_settings;
using nexigen::logging::Settings_paths;
using nexigen::logging::load_logging_settings;

constexpr std::string_view k_root = "/tmp/nexigen-examples-logging-settings";
constexpr std::string_view k_global = "/tmp/nexigen-examples-logging-settings/global.json";
constexpr std::string_view k_local = "/tmp/nexigen-examples-logging-settings/local.json";
constexpr std::string_view k_app_name = "ex-logging-settings";

/// Static storage: FileLogger owns non-movable shard rings and a worker.
FileLogger<> logger{};

void print_line(const std::string& line) {
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
}

void write_file(std::string_view path, std::string_view text) {
    std::ofstream out{std::string(path), std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string find_log_file() {
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(k_root, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file(ec) && entry.path().extension() == ".jsonl") {
            return entry.path().string();
        }
    }
    return {};
}

bool verify_log_file(const std::string& path) {
    std::ifstream in{path};
    if (!in.is_open()) {
        print_line(std::format("cannot open {}", path));
        return false;
    }
    std::size_t lines = 0;
    bool level_ok = false;
    bool app_ok = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        ++lines;
        level_ok = level_ok || line.find("\"level\":\"debug\"") != std::string::npos;
        app_ok = app_ok || line.find(std::format("\"app\":\"{}\"", k_app_name)) !=
                                std::string::npos;
    }
    print_line(std::format("verified {} line(s) in {}", lines, path));
    return lines >= 2U && level_ok && app_ok;
}

} // namespace

int main() {
    std::error_code ec;
    std::filesystem::remove_all(k_root, ec);
    std::filesystem::create_directories(k_root, ec);
    if (ec) {
        print_line(std::format("cannot prepare {}: {}", k_root, ec.message()));
        return 1;
    }

    // Global file: machine-wide policy. `app_name` here is ignored by design
    // (local-only field) and demonstrated as such below.
    write_file(k_global,
               std::format(R"({{"log_root_directory":"{}",)"
                           R"("app_name":"must-be-ignored",)"
                           R"("initial_log_level":"info",)"
                           R"("max_file_size_bytes":4096,)"
                           R"("flush_cpu_affinity":[0]}})",
                           k_root));
    // Local file: per-application override.
    write_file(k_local,
               std::format(R"({{"initial_log_level":"warning",)"
                           R"("app_name":"{}",)"
                           R"("spsc_ring_buffer_capacity":64}})",
                           k_app_name));

    // Environment overrides are the highest-priority layer.
    static_cast<void>(::setenv("NEXIGEN_LOG_LEVEL", "debug", 1));
    static_cast<void>(::setenv("NEXIGEN_LOG_CPU_AFFINITY", "0", 1));

    Logging_settings settings{};
    const auto loaded = load_logging_settings(Settings_paths{std::string(k_global),
                                                             std::string(k_local)},
                                              settings);
    print_line(std::format("load status: {}",
                           loaded.has_value() ? "OK" : nexigen::core::to_string(loaded.error())));
    print_line(settings.to_string());
    for (const auto& diagnostic : settings.diagnostics) {
        print_line(diagnostic.to_string());
    }
    if (!loaded.has_value()) {
        return 1;
    }

    if (!logger.initialize(settings).has_value()) {
        print_line("FileLogger::initialize(Logging_settings) failed");
        return 1;
    }

    logger.debug("settings merged from global + local + environment");
    logger.info("level is {}", LogLevel::Debug);
    logger.warning("size limit is {} bytes", settings.options.max_file_size_bytes);

    const auto flushed = logger.flush();
    if (!flushed.has_value()) {
        print_line("flush failed");
        return 1;
    }

    const std::string produced = find_log_file();
    if (produced.empty()) {
        print_line("no JSON-lines file produced");
        return 1;
    }
    if (!verify_log_file(produced)) {
        print_line("output verification failed");
        return 1;
    }
    print_line("ex_logging_settings: OK");
    return 0;
}
