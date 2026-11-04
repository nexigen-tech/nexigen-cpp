// nexigen-hub — local read-only observatory
// (change nexigen-hub; spec hub/http-api).
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
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
    std::fprintf(stderr, "nexigen-hub: %.*s\n",
                 static_cast<int>(message.size()), message.data());
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> run_dirs;
    std::string listen = "127.0.0.1:8080";
    std::uint64_t ring_records = 4096;
    std::uint64_t ring_bytes = 8ULL * 1024ULL * 1024ULL;
    std::uint64_t max_line_bytes = 1ULL * 1024ULL * 1024ULL;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        auto value = [&](std::string& target) -> bool {
            if (index + 1 >= argc) {
                return false;
            }
            target = argv[++index];
            return true;
        };
        if (argument == "--help" || argument == "-h") {
            print_usage(stdout);
            return 0;
        }
        if (argument == "--run-dir") {
            std::string directory;
            if (!value(directory)) {
                print_usage(stderr);
                return 2;
            }
            run_dirs.push_back(std::move(directory));
        } else if (argument == "--listen") {
            if (!value(listen)) {
                print_usage(stderr);
                return 2;
            }
        } else if (argument == "--ring-records") {
            std::string text;
            if (!value(text) || !tools::parse_u64(text, ring_records)) {
                print_usage(stderr);
                return 2;
            }
        } else if (argument == "--ring-bytes") {
            std::string text;
            if (!value(text) || !tools::parse_u64(text, ring_bytes)) {
                print_usage(stderr);
                return 2;
            }
        } else if (argument == "--max-line-bytes") {
            std::string text;
            if (!value(text) || !tools::parse_u64(text, max_line_bytes)) {
                print_usage(stderr);
                return 2;
            }
        } else {
            std::fprintf(stderr, "nexigen-hub: unknown option '%s'\n",
                         std::string(argument).c_str());
            print_usage(stderr);
            return 2;
        }
    }

    if (run_dirs.empty()) {
        std::fprintf(stderr, "nexigen-hub: at least one --run-dir is required\n");
        print_usage(stderr);
        return 2;
    }

    // Host extraction for the loopback check (analysis #13): strip the RFC
    // bracket form `[<addr>]:<port>`, otherwise split at the FIRST colon —
    // the previous `rfind(':')` truncated a bare `::1` into `:` and disabled
    // the loopback classification exactly when it mattered.
    std::string listen_host{};
    if (!listen.empty() && listen.front() == '[') {
        const std::size_t closing = listen.find(']');
        listen_host = closing == std::string::npos
                          ? listen
                          : listen.substr(1, closing > 1 ? closing - 1 : 0);
    } else if (const std::size_t colon = listen.find(':');
               colon != std::string::npos && colon != 0) {
        listen_host = listen.substr(0, colon);
        if (listen_host == "localhost") {
            listen_host = "127.0.0.1";
        }
    } else {
        listen_host = listen;
    }
    if (listen_host.empty()) {
        listen_host = "127.0.0.1"; // server default applied below anyway
    }
    if (listen_host != "127.0.0.1" && listen_host != "localhost" &&
        listen_host != "::1") {
        std::fprintf(stderr,
                     "nexigen-hub: warning: listening on non-loopback address "
                     "'%s' without authentication\n",
                     listen_host.c_str());
    }

    std::signal(SIGINT, stop_handler);
    std::signal(SIGTERM, stop_handler);

    hub::Discovery discovery;
    const core::Status discovered = discovery.initialize(run_dirs);
    if (!discovered) {
        std::fprintf(stderr, "nexigen-hub: discovery failed: %s\n",
                     std::string(core::to_string(discovered.error())).c_str());
        return 1;
    }

    hub::StreamLimits limits{};
    limits.ring_records = static_cast<std::size_t>(ring_records);
    limits.ring_bytes = static_cast<std::size_t>(ring_bytes);
    limits.max_line_bytes = static_cast<std::size_t>(max_line_bytes);
    hub::StreamManager streaming(limits);
    hub::HttpServer server(discovery, streaming, hub_diagnostic, nullptr);
    const core::Status started = server.start(listen);
    if (!started) {
        std::fprintf(stderr, "nexigen-hub: listen failed: %s\n",
                     std::string(core::to_string(started.error())).c_str());
        return 1;
    }

    std::fprintf(stdout, "nexigen-hub: listening on http://%s:%u\n",
                 listen.substr(0, listen.rfind(':')).c_str(), server.port());
    std::fprintf(stdout, "nexigen-hub: watching %zu run root(s)\n",
                 run_dirs.size());
    std::fflush(stdout);

    server.run(&g_stop);
    return 0;
}
