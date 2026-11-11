// nexigen-hub — local read-only observatory
// (change nexigen-hub; spec hub/http-api).
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/hub/discovery.hpp>
#include <nexigen/hub/http_server.hpp>
#include <nexigen/hub/streaming.hpp>

#include "detail/cli.hpp"

namespace core = nexigen::core;
namespace hub = nexigen::hub;
namespace tools = nexigen::tools;

namespace {

std::atomic<bool> g_stop{false};

extern "C" void stop_handler(int) {
    g_stop.store(true);
}

void print_usage(std::FILE* out) {
    std::fprintf(out, R"(Usage:
  nexigen-hub --run-dir <dir> [--run-dir <dir>...]
              [--listen <host:port>]
              [--listen [<addr>]:<port>] (IPv6 literal, e.g. [::1]:8080 or
                                         [::]:8080 for dual-stack)
              [--ring-records <n>] [--ring-bytes <n>]
              [--max-line-bytes <n>]
  nexigen-hub --help

Defaults: listen 127.0.0.1:8080, ring 4096 records / 8 MiB,
maximum line 1 MiB. The hub is read-only and serves an embedded web UI.
)");
}

void hub_diagnostic(void* context, std::string_view message) noexcept {
    static_cast<void>(context);
    std::fprintf(stderr, "nexigen-hub: %.*s\n", static_cast<int>(message.size()), message.data());
}

struct HubOptions {
    std::vector<std::string> run_dirs{};
    std::string listen = "127.0.0.1:8080";
    std::uint64_t ring_records = 4096;
    std::uint64_t ring_bytes = 8ULL * 1024ULL * 1024ULL;
    std::uint64_t max_line_bytes = 1ULL * 1024ULL * 1024ULL;
};

/// Consume the next argv token as a string; false when it is missing.
[[nodiscard]] bool take_value(int argc, char* argv[], int& index, std::string& target) {
    if (index + 1 >= argc) {
        return false;
    }
    target = argv[++index];
    return true;
}

/// Consume the next argv token as a u64; false when missing or malformed.
[[nodiscard]] bool take_u64_value(int argc, char* argv[], int& index, std::uint64_t& target) {
    std::string text;
    return take_value(argc, argv, index, text) && tools::parse_u64(text, target);
}

/// Parse argv. Returns std::nullopt to continue, or the process exit code.
[[nodiscard]] std::optional<int> parse_hub_args(int argc, char* argv[], HubOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            print_usage(stdout);
            return 0;
        }
        if (argument == "--run-dir") {
            std::string directory;
            if (!take_value(argc, argv, index, directory)) {
                print_usage(stderr);
                return 2;
            }
            options.run_dirs.push_back(std::move(directory));
            continue;
        }
        if (argument == "--listen") {
            if (!take_value(argc, argv, index, options.listen)) {
                print_usage(stderr);
                return 2;
            }
            continue;
        }
        std::uint64_t* target = nullptr;
        if (argument == "--ring-records") {
            target = &options.ring_records;
        } else if (argument == "--ring-bytes") {
            target = &options.ring_bytes;
        } else if (argument == "--max-line-bytes") {
            target = &options.max_line_bytes;
        }
        if (target != nullptr) {
            if (!take_u64_value(argc, argv, index, *target)) {
                print_usage(stderr);
                return 2;
            }
            continue;
        }
        std::fprintf(stderr, "nexigen-hub: unknown option '%s'\n", std::string(argument).c_str());
        print_usage(stderr);
        return 2;
    }

    if (options.run_dirs.empty()) {
        std::fprintf(stderr, "nexigen-hub: at least one --run-dir is required\n");
        print_usage(stderr);
        return 2;
    }
    return std::nullopt;
}

/// Host extraction for the loopback check (analysis #13): strip the RFC
/// bracket form `[<addr>]:<port>`, otherwise split at the FIRST colon — the
/// previous `rfind(':')` truncated a bare `::1` into `:` and disabled the
/// loopback classification exactly when it mattered.
[[nodiscard]] std::string classify_listen_host(std::string_view listen) {
    std::string host{};
    if (!listen.empty() && listen.front() == '[') {
        const std::size_t closing = listen.find(']');
        host = closing == std::string_view::npos
                   ? std::string(listen)
                   : std::string(listen.substr(1, closing > 1 ? closing - 1 : 0));
    } else if (const std::size_t colon = listen.find(':');
               colon != std::string_view::npos && colon != 0) {
        host = std::string(listen.substr(0, colon));
        if (host == "localhost") {
            host = "127.0.0.1";
        }
    } else {
        host = std::string(listen);
    }
    if (host.empty()) {
        host = "127.0.0.1"; // server default applied below anyway
    }
    return host;
}

void warn_if_non_loopback(const std::string& host) {
    if (host == "127.0.0.1" || host == "localhost" || host == "::1") {
        return;
    }
    std::fprintf(stderr,
                 "nexigen-hub: warning: listening on non-loopback address "
                 "'%s' without authentication\n",
                 host.c_str());
}

[[nodiscard]] int run_hub(const HubOptions& options) {
    std::signal(SIGINT, stop_handler);
    std::signal(SIGTERM, stop_handler);

    hub::Discovery discovery;
    const core::Status discovered = discovery.initialize(options.run_dirs);
    if (!discovered) {
        std::fprintf(stderr, "nexigen-hub: discovery failed: %s\n",
                     std::string(core::to_string(discovered.error())).c_str());
        return 1;
    }

    hub::StreamLimits limits{};
    limits.ring_records = static_cast<std::size_t>(options.ring_records);
    limits.ring_bytes = static_cast<std::size_t>(options.ring_bytes);
    limits.max_line_bytes = static_cast<std::size_t>(options.max_line_bytes);
    hub::StreamManager streaming(limits);
    hub::HttpServer server(discovery, streaming, hub_diagnostic, nullptr);
    const core::Status started = server.start(options.listen);
    if (!started) {
        std::fprintf(stderr, "nexigen-hub: listen failed: %s\n",
                     std::string(core::to_string(started.error())).c_str());
        return 1;
    }

    std::fprintf(stdout, "nexigen-hub: listening on http://%s:%u\n",
                 options.listen.substr(0, options.listen.rfind(':')).c_str(), server.port());
    std::fprintf(stdout, "nexigen-hub: watching %zu run root(s)\n", options.run_dirs.size());
    std::fflush(stdout);

    server.run(&g_stop);
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    HubOptions options{};
    if (const std::optional<int> code = parse_hub_args(argc, argv, options)) {
        return *code;
    }
    warn_if_non_loopback(classify_listen_host(options.listen));
    return run_hub(options);
}
