// Allocation-freedom and locking tests for the error gate (change
// add-canopen-observability; spec canopen/observability: "Error tracking is
// fixed-size", "Hot-path metric recording is wait-free and allocation-free").
#include "observability_test_support.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include <nexigen/canopen/observability/can_open_error_domain.hpp>
#include <nexigen/canopen/observability/can_open_log_policy.hpp>
#include <nexigen/canopen/observability/detail/episode_gate.hpp>
#include <nexigen/canopen/observability/detail/error_gate.hpp>
#include <nexigen/logging/null_logger.hpp>

namespace {

namespace obs = nexigen::canopen::observability;
namespace detail = nexigen::canopen::observability::detail;

using obs::CanOpenErrorDomain;
using obs::CanOpenLogPolicy;
using detail::ErrorGate;
using NullLogger = nexigen::logging::NullLogger;

static_assert(std::is_trivially_destructible_v<ErrorGate<NullLogger>>,
              "the gate must own only fixed-size value storage");
static_assert(noexcept(std::declval<ErrorGate<NullLogger>&>().on_error(
    CanOpenErrorDomain::Bus, std::uint8_t{0}, std::uint32_t{0}, std::uint64_t{0})));
static_assert(noexcept(std::declval<ErrorGate<NullLogger>&>().poll_recovery(std::uint64_t{0})));
static_assert(noexcept(std::declval<ErrorGate<NullLogger>&>().emit_summary(std::uint64_t{0})));
static_assert(noexcept(std::declval<ErrorGate<NullLogger>&>().poll(std::uint64_t{0})));

/// @brief Run every gate path with the allocation trap armed.
void test_gate_operations_never_allocate() {
    NullLogger logger;
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 1U;
    policy.recovery_hold_ms = 10U;
    policy.summary_interval_ms = 1'000U;
    policy.max_suppressed_per_key = 4U;
    policy.max_hot_logs_per_second = 4U;
    ErrorGate<NullLogger> gate{policy, logger};

    allocation_counter_arm();
    for (std::uint64_t index = 0U; index < 8U; ++index) {
        static_cast<void>(gate.on_error(CanOpenErrorDomain::Bus, 1U, 0x10U, index * ms_to_ns(1U)));
    }
    static_cast<void>(gate.on_error(CanOpenErrorDomain::Bus, 1U, 0x20U, 8U * ms_to_ns(1U)));
    for (std::uint64_t index = 9U; index < 20U; ++index) {
        static_cast<void>(gate.on_error(CanOpenErrorDomain::Nmt, 2U, 0x30U, index * ms_to_ns(1U)));
    }
    static_cast<void>(gate.poll(20U * ms_to_ns(1U)));
    static_cast<void>(gate.emit_summary(2'000U * ms_to_ns(1U)));
    const std::uint64_t allocations = allocation_counter_disarm();
    NX_CHECK_EQ(allocations, std::uint64_t{0});
}

/// @brief Read one source file into memory.
/// @param relative Path below `NEXIGEN_SOURCE_DIR`.
/// @return File text, or an empty string when unreadable.
std::string read_source(const char* relative) {
    std::ifstream stream{std::string(NEXIGEN_SOURCE_DIR) + "/" + relative};
    if (!stream.good()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/// @brief Assert a header contains none of the locking/allocation primitives.
/// @param text Header text.
void check_no_locking(const std::string& text) {
    NX_CHECK(!text.empty());
    NX_CHECK(text.find("#include <mutex>") == std::string::npos);
    NX_CHECK(text.find("#include <atomic>") == std::string::npos);
    NX_CHECK(text.find("std::mutex") == std::string::npos);
    NX_CHECK(text.find("std::atomic<") == std::string::npos);
    NX_CHECK(text.find("std::lock_guard") == std::string::npos);
    NX_CHECK(text.find("spinlock") == std::string::npos);
}

/// @brief Prove the gate implementation mentions no lock primitive.
void test_gate_headers_have_no_locking_primitives() {
    check_no_locking(read_source("include/nexigen/canopen/observability/detail/error_gate.hpp"));
    check_no_locking(read_source("include/nexigen/canopen/observability/detail/episode_gate.hpp"));
}

} // namespace

void suite_allocation() {
    test_gate_operations_never_allocate();
    test_gate_headers_have_no_locking_primitives();
}
