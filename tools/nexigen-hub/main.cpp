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

namespace core = nexigen::core;
namespace hub = nexigen::hub;

namespace {

std::atomic<bool> g_stop{false};

extern "C" void stop_handler(int) {
    g_stop.store(true);
}

void print_usage(std::FILE* out) {
    std::fprintf(out, R"(Usage:
  nexigen-hub --run-dir <dir> [--run-dir <dir>...]
              [--listen <host:port>]
              [--ring-records <n>] [--ring-bytes <n>]
              [--max-line-bytes <n>]
  nexigen-hub --help

Defaults: listen 127.0.0.1:8080, ring 4096 records / 8 MiB,
maximum line 1 MiB. The hub is read-only and serves an embedded web UI.
)");
}

[[nodiscard]] bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10ULL) {
            return false;
        }
        value = value * 10ULL + digit;
    }
    out = value;
    return true;
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
            if (!value(text) || !parse_u64(text, ring_records)) {
                print_usage(stderr);
                return 2;
            }
        } else if (argument == "--ring-bytes") {
            std::string text;
            if (!value(text) || !parse_u64(text, ring_bytes)) {
                print_usage(stderr);
                return 2;
            }
        } else if (argument == "--max-line-bytes") {
            std::string text;
            if (!value(text) || !parse_u64(text, max_line_bytes)) {
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

    const std::size_t listen_colon = listen.rfind(':');
    const std::string listen_host =
        listen_colon == std::string::npos ? listen : listen.substr(0, listen_colon);
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

    hub::Stream_limits limits{};
    limits.ring_records = static_cast<std::size_t>(ring_records);
    limits.ring_bytes = static_cast<std::size_t>(ring_bytes);
    limits.max_line_bytes = static_cast<std::size_t>(max_line_bytes);
    hub::Stream_manager streaming(limits);
    hub::Http_server server(discovery, streaming);
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
