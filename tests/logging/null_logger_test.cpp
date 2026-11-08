// Split from logging_test.cpp: nexigen::logging NullLogger compile-away contract.

#include "logging_test_support.hpp"

namespace {

// --- concept pins (task 6.1/6.2 / spec logging/logger-concept: published
// loggers are compile-time conformant on the variadic void surface).
static_assert(concepts::Logger<NullLogger>);
static_assert(concepts::Logger<ConsoleLogger<>>);
static_assert(concepts::Logger<ConsoleLogger<32, 128>>);
static_assert(concepts::Logger<FileLogger<>>);
static_assert(concepts::Logger<FileLogger<64, 128>>);
static_assert(ConsoleLogger<>::capacity() == 128); // per-shard default (design D6)
static_assert(FileLogger<>::capacity() == 128);
static_assert(ConsoleLogger<>::max_threads() == 32);
static_assert(FileLogger<64, 128, 4>::max_threads() == 4);

// Literal call shapes compile on real loggers too (spec format-api): the
// requires-expression proves conversion from a literal via the consteval
// constructor WITHOUT evaluating the call itself.
static_assert(requires(ConsoleLogger<32, 64> cl) {
    { cl.info("plain") } noexcept -> std::same_as<void>;
    {
        cl.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025)
    } noexcept -> std::same_as<void>;
    { cl.log(LogLevel::Error, "Level is {}", LogLevel::Warning) } noexcept -> std::same_as<void>;
});

// --- composite pins (task 7.1 / spec logging/composite-logger): the fan-out
// satisfies the contract — including the empty pack and nested composites.
static_assert(concepts::Logger<CompositeLogger<>>);
static_assert(concepts::Logger<CompositeLogger<NullLogger>>);
static_assert(concepts::Logger<CompositeLogger<ConsoleLogger<>, FileLogger<>>>);
static_assert(concepts::Logger<CompositeLogger<CompositeLogger<NullLogger, ConsoleLogger<64, 128>>,
                                               FileLogger<64, 128>>>);

// --- null logger suite (task 6.2 / spec logging/null-logger).
void test_null_logger() {
    NullLogger logger{};
    // Every call completes silently at every level, including Fatal with
    // argument packs, and flush is trivially complete.
    logger.verbose("v");
    logger.debug("d");
    logger.info("i");
    logger.warning("w");
    logger.error("e");
    logger.fatal("f");
    logger.info("packed {}", 42);
    logger.log(LogLevel::Fatal, "explicit");
    NX_CHECK(logger.flush().has_value());

    // Level control still honored (stored/reported; behavior is uniform no-op).
    NX_CHECK(logger.level() == LogLevel::Info);
    logger.set_level(LogLevel::Warning);
    NX_CHECK(logger.level() == LogLevel::Warning);
    logger.set_level(LogLevel::Off);
    NX_CHECK(logger.level() == LogLevel::Off);
    logger.info("no output anywhere either way");

    // Diagnostic dump names the stored threshold (spec null-logger to_string).
    NX_CHECK(logger.to_string().find("off") != std::string::npos);
    NX_CHECK(to_string(logger) == logger.to_string());
    logger.set_level(LogLevel::Info);

    // Arithmetic identity for the objdump check (loop ran, nothing else).
    NX_CHECK(null_loop(1000) == 499'500);
}

} // namespace

// Compile-away proof helper (declared in logging_test_support.hpp): plain
// sum-of-ints loop plus NullLogger::info calls WITH an argument pack — at -O3
// only the accumulation survives (spec logging/null-logger "Optimized loop
// leaves no logging residue").
int null_loop(int iterations) noexcept {
    NullLogger n{};
    int acc = 0;
    for (int i = 0; i < iterations; ++i) {
        acc += i;
        n.info("tick {}", i);
    }
    return acc;
}

void suite_null_logger() {
    run_suite("null_logger", test_null_logger);
}
