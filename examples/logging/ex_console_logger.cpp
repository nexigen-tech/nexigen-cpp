// nexigen::logging — runnable asynchronous console logger example.
//
// A default ConsoleLogger accepts records from several std::jthread producers
// through the variadic deferred-format API, demonstrates runtime set_level()
// filtering and direct LogLevel formatting, flushes, and prints its drop and
// diagnostic snapshots. Initialization and flush are checked via has_value()
// / .error() — never value().
#include <array>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <thread>

#include <nexigen/logging/console_logger.hpp>
#include <nexigen/logging/log_level.hpp>

namespace {

/// Static storage: ConsoleLogger owns non-movable shard rings and a worker.
nexigen::logging::ConsoleLogger<> logger{};

constexpr std::uint32_t k_workers = 4;
constexpr std::uint32_t k_ticks = 3;

void print_line(const std::string& line) {
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
}

} // namespace

int main() {
    using nexigen::logging::LogLevel;

    const auto started = logger.initialize(
        nexigen::logging::ConsoleLogger<>::Config{.initial_level = LogLevel::Verbose});
    if (!started.has_value()) {
        print_line(std::format("initialize failed: {}", started.error()));
        return 1;
    }

    // Several producer threads, one shard each, every convenience severity.
    // Arguments are packed here and formatted on the background writer.
    std::array<std::jthread, k_workers> workers{};
    for (std::uint32_t worker = 0; worker < k_workers; ++worker) {
        workers[worker] = std::jthread{[worker] {
            for (std::uint32_t tick = 0; tick < k_ticks; ++tick) {
                logger.verbose("worker {} verbose tick {}", worker, tick);
                logger.debug("worker {} debug tick {}", worker, tick);
                logger.info("worker {} info tick {}", worker, tick);
                logger.warning("worker {} warning tick {}", worker, tick);
                logger.error("worker {} error tick {}", worker, tick);
                logger.fatal("worker {} fatal tick {}", worker, tick);
            }
        }};
    }
    for (auto& worker : workers) {
        worker.join();
    }

    // Runtime filtering: one relaxed cell, effective immediately.
    logger.set_level(LogLevel::Warning);
    logger.info("this info record is filtered below the active threshold");
    logger.warning("post-filter warning at level {}", LogLevel::Warning);
    print_line(std::format("active level after set_level: {}", logger.level()));

    // Control-plane barrier: wait until everything accepted so far is written.
    const auto flushed = logger.flush();
    if (!flushed.has_value()) {
        print_line(std::format("flush failed: {}", flushed.error()));
        return 1;
    }

    // Cold-path diagnostics (snapshots, never synchronization).
    print_line(std::format("dropped: {}", logger.dropped().to_string()));
    print_line(std::format("logger: {}", logger.to_string()));
    print_line("ex_console_logger: OK");
    return 0;
}
