// nexigen-traced — центральный TraceDaemon (change add-trace-daemon; spec
// tracing/trace-daemon).
//
// Подписывается на SpanFrame-поток, держит последние секунды в RAM-ring и
// пишет версионированный бинарный снимок при аварийном уведомлении PHM.
// В штатном режиме на диск не пишет.
//
// Использование: nexigen-traced --config <path-to-trace-daemon-config.json>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <csignal>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/tracing/detail/trace_daemon_session.hpp>
#include <nexigen/tracing/trace_daemon_config.hpp>

namespace core = nexigen::core;
namespace tracing = nexigen::tracing;

namespace {

std::atomic<bool> g_stop{false};

void signal_handler(int) {
    g_stop.store(true, std::memory_order_relaxed);
}

struct Options {
    const char* config_path{nullptr};
};

void print_usage() {
    std::printf("usage: nexigen-traced --config <trace-daemon-config.json>\n"
                "  --help  show this help\n");
}

/// Parse the CLI. Returns an exit code when the process must stop immediately.
std::optional<int> parse_args(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }
        if (arg == "--config") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "nexigen-traced: --config requires a path\n");
                return 2;
            }
            options.config_path = argv[++i];
            continue;
        }
        std::fprintf(stderr, "nexigen-traced: unknown argument '%.*s'\n",
                     static_cast<int>(arg.size()), arg.data());
        return 2;
    }
    if (options.config_path == nullptr) {
        std::fprintf(stderr, "nexigen-traced: --config is required\n");
        return 2;
    }
    return std::nullopt;
}

bool load_config(const char* path, tracing::TraceDaemonConfig& config) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    const nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return false;
    }
    tracing::TraceDaemonConfig parsed{};
    tracing::from_json(json, parsed);
    config = std::move(parsed);
    return true;
}

int run_daemon(const Options& options) {
    tracing::TraceDaemonConfig config{};
    if (!load_config(options.config_path, config)) {
        std::fprintf(stderr, "nexigen-traced: failed to load config from %s\n",
                     options.config_path);
        return 1;
    }
    if (!config.validate()) {
        std::fprintf(stderr, "nexigen-traced: invalid config\n");
        return 1;
    }

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);

    tracing::detail::TraceDaemonSession session{config};
    const core::Status started = session.start();
    if (!started) {
        std::fprintf(stderr, "nexigen-traced: failed to start (%s)\n",
                     std::string(core::to_string(started.error())).c_str());
        return 1;
    }

    std::printf("nexigen-traced: listening on %s (ring=%llu records, interval=%u ms, dumps=%s)\n",
                config.service_name.c_str(),
                static_cast<unsigned long long>(config.ring_capacity_records),
                config.drain_interval_ms, config.dump_directory.c_str());
    std::fflush(stdout);

    session.run(g_stop);
    session.stop();
    std::printf("nexigen-traced: stopped (spans=%llu, triggers=%llu, dumps=%llu)\n",
                static_cast<unsigned long long>(session.spans_received()),
                static_cast<unsigned long long>(session.triggers()),
                static_cast<unsigned long long>(session.dumps_written()));
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    Options options;
    const std::optional<int> early_exit = parse_args(argc, argv, options);
    if (early_exit.has_value()) {
        return *early_exit;
    }
    return run_daemon(options);
}
