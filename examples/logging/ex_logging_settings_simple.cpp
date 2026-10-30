// nexigen::logging — minimal global/local settings example.
//
// The shortest loader-to-logger flow: write one global and one local JSON
// file, load them through explicit paths, initialize a FileLogger from the
// merged settings, log a couple of records, flush, and exit 0. No environment
// overrides and no output verification — see ex_logging_settings.cpp for the
// fuller demonstration.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/logging_settings.hpp>

namespace {

using nexigen::logging::FileLogger;
using nexigen::logging::Logging_settings;
using nexigen::logging::Settings_paths;
using nexigen::logging::load_logging_settings;

constexpr std::string_view k_root = "/tmp/nexigen-examples-logging-settings-simple";

/// Static storage: FileLogger owns non-movable shard rings and a worker.
FileLogger<> logger{};

int fail(std::string_view message) {
    std::fprintf(stderr, "ex_logging_settings_simple: %s\n",
                 std::string(message).c_str());
    return 1;
}

void write_file(const std::string& path, std::string_view text) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace

int main() {
    std::error_code ec;
    std::filesystem::remove_all(k_root, ec);
    std::filesystem::create_directories(k_root, ec);
    if (ec) {
        return fail("cannot create the temporary settings directory");
    }

    const std::string global_path = std::string(k_root) + "/global.json";
    const std::string local_path = std::string(k_root) + "/local.json";

    // Global: machine-wide defaults. Local: this application's overrides.
    write_file(global_path,
               std::string(R"({"log_root_directory":")") + std::string(k_root) +
                   R"(","initial_log_level":"info","max_file_size_bytes":4096})");
    write_file(local_path,
               R"({"app_name":"ex-logging-settings-simple",)"
               R"("initial_log_level":"debug"})");

    Logging_settings settings{};
    const auto loaded =
        load_logging_settings(Settings_paths{global_path, local_path}, settings);
    for (const auto& diagnostic : settings.diagnostics) {
        std::fprintf(stderr, "%s\n", diagnostic.to_string().c_str());
    }
    if (!loaded.has_value()) {
        return fail("failed to load global/local settings");
    }

    if (!logger.initialize(settings).has_value()) {
        return fail("failed to initialize the logger from settings");
    }

    logger.debug("initialized from global.json + local.json");
    logger.info("application is {}", std::string_view{settings.options.app_name});
    if (!logger.flush().has_value()) {
        return fail("flush failed");
    }

    std::printf("ex_logging_settings_simple: OK (app=%s, level=%s)\n",
                settings.options.app_name.c_str(),
                std::string(nexigen::logging::to_string(logger.level())).c_str());
    return 0;
}
