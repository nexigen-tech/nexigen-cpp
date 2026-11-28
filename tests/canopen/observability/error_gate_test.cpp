// Error-gate policy tests (change add-canopen-observability; spec
// canopen/observability): layout, episode actions, recovery, budget, summary.
#include "observability_test_support.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include <nexigen/canopen/observability/can_open_error_domain.hpp>
#include <nexigen/canopen/observability/can_open_log_policy.hpp>
#include <nexigen/canopen/observability/detail/episode_gate.hpp>
#include <nexigen/canopen/observability/detail/error_gate.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/null_logger.hpp>

namespace {

namespace obs = nexigen::canopen::observability;
namespace detail = nexigen::canopen::observability::detail;

using obs::CanOpenErrorDomain;
using obs::CanOpenLogPolicy;
using detail::EpisodeGate;
using detail::ErrorGate;

// --- 4.1 layout and storage --------------------------------------------------

static_assert(sizeof(EpisodeGate) == 40);
static_assert(alignof(EpisodeGate) == 8);
static_assert(std::is_trivially_copyable_v<EpisodeGate>);
static_assert(std::is_trivially_destructible_v<EpisodeGate>);
static_assert(std::is_arithmetic_v<decltype(EpisodeGate::last_error_ns)>);
static_assert(std::is_arithmetic_v<decltype(EpisodeGate::suppressed)>);
static_assert(std::is_arithmetic_v<decltype(EpisodeGate::active)>);
static_assert(sizeof(ErrorGate<nexigen::logging::NullLogger>) >=
              ErrorGate<nexigen::logging::NullLogger>::node_capacity *
                  ErrorGate<nexigen::logging::NullLogger>::domain_capacity * sizeof(EpisodeGate));
static_assert(ErrorGate<nexigen::logging::NullLogger>::node_capacity == 128U);
static_assert(ErrorGate<nexigen::logging::NullLogger>::domain_capacity == 9U);
static_assert(std::is_trivially_destructible_v<ErrorGate<nexigen::logging::NullLogger>>);

void test_gate_layout_and_node_range() {
    nexigen::logging::NullLogger logger;
    const CanOpenLogPolicy policy{};
    ErrorGate<nexigen::logging::NullLogger> gate{policy, logger};
    for (std::uint8_t node = 0U; node < 128U; ++node) {
        for (std::size_t domain = 0U; domain < obs::error_domain_count; ++domain) {
            const auto outcome =
                gate.on_error(static_cast<CanOpenErrorDomain>(domain), node, 1U, 0U);
            NX_CHECK(outcome ==
                     ErrorGate<nexigen::logging::NullLogger>::ErrorAction::FirstOccurrence);
        }
    }
    for (std::uint8_t node = 0U; node < 128U; ++node) {
        for (std::size_t domain = 0U; domain < obs::error_domain_count; ++domain) {
            const detail::EpisodeGate& slot =
                gate.episode(static_cast<CanOpenErrorDomain>(domain), node);
            NX_CHECK(slot.active == 1U);
            NX_CHECK_EQ(slot.last_code, 1U);
        }
    }
    NX_CHECK(gate.to_string().find("active_episodes=1152") != std::string::npos);

    // Each (node, domain) slot is independent: a code change on node 0 must not
    // touch node 1 or another domain of node 0.
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Bus, 0U, 2U, 1U) ==
             ErrorGate<nexigen::logging::NullLogger>::ErrorAction::ChangedCode);
    NX_CHECK_EQ(gate.episode(CanOpenErrorDomain::Bus, 0U).last_code, 2U);
    NX_CHECK_EQ(gate.episode(CanOpenErrorDomain::Bus, 1U).last_code, 1U);
    NX_CHECK_EQ(gate.episode(CanOpenErrorDomain::Nmt, 0U).last_code, 1U);
}

// --- 4.2 episode actions -----------------------------------------------------

void test_first_error_opens_episode_and_logs() {
    CaptureLogger logger;
    const CanOpenLogPolicy policy{};
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    const auto action = gate.on_error(CanOpenErrorDomain::Sdo, 5U, 0x0503'0000U, 0U);
    NX_CHECK(action == ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK_EQ(captured_records().size(), std::size_t{1});
    NX_CHECK(captured_contains("canopen error episode start"));
    NX_CHECK(captured_contains("domain=sdo"));
    NX_CHECK(captured_contains("node=5"));
    NX_CHECK(captured_contains("code=0x05030000"));
    NX_CHECK_EQ(captured_level_count(nexigen::core::LogLevel::Error), std::size_t{1});
    NX_CHECK(gate.episode(CanOpenErrorDomain::Sdo, 5U).active == 1U);
    NX_CHECK(gate.episode(CanOpenErrorDomain::Sdo, 5U).logged == 1U);
}

void test_repeat_inside_interval_is_suppressed() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 5'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Pdo, 3U, 7U, 0U) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Pdo, 3U, 7U, ms_to_ns(4'999U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);
    NX_CHECK_EQ(captured_records().size(), std::size_t{1});
    NX_CHECK_EQ(gate.episode(CanOpenErrorDomain::Pdo, 3U).suppressed, 1U);
}

void test_periodic_repeat_reports_suppressed_count() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 1'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Pdo, 3U, 7U, 0U) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Pdo, 3U, 7U, ms_to_ns(500U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Pdo, 3U, 7U, ms_to_ns(1'000U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::Repeat);
    NX_CHECK_EQ(captured_records().size(), std::size_t{2});
    NX_CHECK(captured_contains("canopen error repeat"));
    NX_CHECK(captured_contains("suppressed=2"));
    NX_CHECK_EQ(gate.episode(CanOpenErrorDomain::Pdo, 3U).suppressed, 0U);
}

void test_code_change_logs_immediately() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 60'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Emcy, 9U, 0x100U, 0U) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Emcy, 9U, 0x200U, ms_to_ns(1U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::ChangedCode);
    NX_CHECK_EQ(captured_records().size(), std::size_t{2});
    NX_CHECK(captured_contains("canopen error code change"));
    NX_CHECK(captured_contains("previous=0x00000100"));
    NX_CHECK_EQ(gate.episode(CanOpenErrorDomain::Emcy, 9U).last_code, 0x200U);
}

void test_forced_repeat_at_suppressed_threshold() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 60'000U;
    policy.max_suppressed_per_key = 3U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Nmt, 1U, 42U, 0U) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Nmt, 1U, 42U, ms_to_ns(1U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Nmt, 1U, 42U, ms_to_ns(2U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Nmt, 1U, 42U, ms_to_ns(3U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::ForcedRepeat);
    NX_CHECK_EQ(captured_records().size(), std::size_t{2});
    NX_CHECK(captured_contains("suppressed=3"));
    NX_CHECK_EQ(gate.episode(CanOpenErrorDomain::Nmt, 1U).suppressed, 0U);
}

void test_filtered_level_does_not_mark_logged() {
    CaptureLogger logger;
    logger.set_level(nexigen::core::LogLevel::Fatal);
    const CanOpenLogPolicy policy{};
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Transport, 2U, 9U, 0U) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK_EQ(captured_records().size(), std::size_t{0});
    NX_CHECK(gate.episode(CanOpenErrorDomain::Transport, 2U).logged == 0U);
    NX_CHECK(gate.episode(CanOpenErrorDomain::Transport, 2U).active == 1U);
}

void test_off_level_disables_every_record_class() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.hot_error_level = nexigen::core::LogLevel::Off;
    policy.recovery_level = nexigen::core::LogLevel::Off;
    policy.summary_level = nexigen::core::LogLevel::Off;
    policy.recovery_hold_ms = 1U;
    policy.summary_interval_ms = 1'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Bus, 0U, 1U, 0U) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK_EQ(gate.poll(ms_to_ns(1'000U)).recovered, std::size_t{1});
    NX_CHECK_EQ(captured_records().size(), std::size_t{0});
}

// --- 4.3 recovery ------------------------------------------------------------

void test_recovery_waits_for_hold_interval() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.recovery_hold_ms = 5'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Bus, 0U, 1U, 0U) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK_EQ(gate.poll_recovery(ms_to_ns(4'999U)), std::size_t{0});
    NX_CHECK(gate.episode(CanOpenErrorDomain::Bus, 0U).active == 1U);
    NX_CHECK_EQ(gate.poll_recovery(ms_to_ns(5'000U)), std::size_t{1});
    NX_CHECK(gate.episode(CanOpenErrorDomain::Bus, 0U).active == 0U);
    NX_CHECK(captured_contains("canopen error episode recovered"));
    NX_CHECK(captured_contains("domain=bus"));
    NX_CHECK(captured_contains("code=0x00000001"));
    NX_CHECK_EQ(captured_level_count(nexigen::core::LogLevel::Info), std::size_t{1});
}

void test_flapping_restarts_the_hold_window() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.recovery_hold_ms = 5'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Bus, 0U, 1U, 0U) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Bus, 0U, 1U, ms_to_ns(4'000U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);
    NX_CHECK_EQ(gate.poll_recovery(ms_to_ns(8'999U)), std::size_t{0});
    NX_CHECK_EQ(gate.poll_recovery(ms_to_ns(9'000U)), std::size_t{1});
}

void test_recovery_reports_suppressed_count() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 60'000U;
    policy.recovery_hold_ms = 1'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Signal, 7U, 5U, 0U) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Signal, 7U, 5U, ms_to_ns(1'000U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Signal, 7U, 5U, ms_to_ns(2'000U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Signal, 7U, 5U, ms_to_ns(3'000U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);
    NX_CHECK_EQ(gate.poll_recovery(ms_to_ns(4'000U)), std::size_t{1});
    NX_CHECK(captured_contains("suppressed=3"));
}

void test_filtered_recovery_still_closes_episode() {
    CaptureLogger logger;
    logger.set_level(nexigen::core::LogLevel::Fatal);
    CanOpenLogPolicy policy{};
    policy.recovery_hold_ms = 1'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    static_cast<void>(gate.on_error(CanOpenErrorDomain::Config, 1U, 3U, 0U));
    NX_CHECK_EQ(gate.poll_recovery(ms_to_ns(1'000U)), std::size_t{1});
    NX_CHECK(gate.episode(CanOpenErrorDomain::Config, 1U).active == 0U);
    NX_CHECK_EQ(captured_records().size(), std::size_t{0});
}

// --- 4.4 global budget and summary ------------------------------------------

void test_repeat_budget_limits_repeats() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 1U;
    policy.max_suppressed_per_key = 100'000U;
    policy.max_hot_logs_per_second = 2U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Ipc, 4U, 8U, 0U) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Ipc, 4U, 8U, ms_to_ns(1U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::Repeat);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Ipc, 4U, 8U, ms_to_ns(2U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::Repeat);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Ipc, 4U, 8U, ms_to_ns(3U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Ipc, 4U, 8U, ms_to_ns(4U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);
    NX_CHECK_EQ(captured_records().size(), std::size_t{3});
    NX_CHECK_EQ(gate.repeat_budget_drops(), std::uint64_t{2});
}

void test_first_error_and_recovery_bypass_exhausted_budget() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 1U;
    policy.max_suppressed_per_key = 100'000U;
    policy.max_hot_logs_per_second = 1U;
    policy.recovery_hold_ms = 1U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Ipc, 4U, 8U, 0U) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Ipc, 4U, 8U, ms_to_ns(1U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::Repeat);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Ipc, 4U, 8U, ms_to_ns(2U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);

    NX_CHECK(gate.on_error(CanOpenErrorDomain::Bus, 6U, 1U, ms_to_ns(3U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::FirstOccurrence);
    NX_CHECK(gate.on_error(CanOpenErrorDomain::Bus, 6U, 1U, ms_to_ns(4U)) ==
             ErrorGate<CaptureLogger>::ErrorAction::None);
    NX_CHECK_EQ(gate.poll_recovery(ms_to_ns(5U)), std::size_t{2});
    NX_CHECK_EQ(captured_count("canopen error episode start"), std::size_t{2});
    NX_CHECK_EQ(captured_count("canopen error episode recovered"), std::size_t{2});
}

void test_summary_requires_activity() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.summary_interval_ms = 1'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    NX_CHECK_EQ(gate.emit_summary(ms_to_ns(1'000U)), std::size_t{0});
    NX_CHECK_EQ(captured_records().size(), std::size_t{0});
}

void test_summary_reports_active_domains_once() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.summary_interval_ms = 1'000U;
    policy.repeat_interval_ms = 60'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    static_cast<void>(gate.on_error(CanOpenErrorDomain::Bus, 0U, 1U, 0U));
    static_cast<void>(gate.on_error(CanOpenErrorDomain::Bus, 0U, 1U, ms_to_ns(1U)));
    static_cast<void>(gate.on_error(CanOpenErrorDomain::Nmt, 1U, 2U, ms_to_ns(2U)));
    NX_CHECK_EQ(gate.emit_summary(ms_to_ns(999U)), std::size_t{0});
    NX_CHECK_EQ(gate.emit_summary(ms_to_ns(1'000U)), std::size_t{2});
    NX_CHECK(captured_contains("canopen error summary"));
    NX_CHECK(captured_contains("domain=bus"));
    NX_CHECK(captured_contains("domain=nmt"));
    NX_CHECK(captured_contains("errors=2"));
    NX_CHECK(captured_contains("suppressed=1"));
    NX_CHECK_EQ(gate.emit_summary(ms_to_ns(2'000U)), std::size_t{0});
}

void test_poll_combines_recovery_and_summary() {
    CaptureLogger logger;
    CanOpenLogPolicy policy{};
    policy.summary_interval_ms = 1'000U;
    policy.recovery_hold_ms = 1'000U;
    ErrorGate<CaptureLogger> gate{policy, logger};
    clear_captured_records();
    static_cast<void>(gate.on_error(CanOpenErrorDomain::Bus, 0U, 1U, 0U));
    const auto result = gate.poll(ms_to_ns(1'000U));
    NX_CHECK_EQ(result.recovered, std::size_t{1});
    NX_CHECK_EQ(result.summaries, std::size_t{1});
}

} // namespace

void suite_error_gate() {
    test_gate_layout_and_node_range();
    test_first_error_opens_episode_and_logs();
    test_repeat_inside_interval_is_suppressed();
    test_periodic_repeat_reports_suppressed_count();
    test_code_change_logs_immediately();
    test_forced_repeat_at_suppressed_threshold();
    test_filtered_level_does_not_mark_logged();
    test_off_level_disables_every_record_class();
    test_recovery_waits_for_hold_interval();
    test_flapping_restarts_the_hold_window();
    test_recovery_reports_suppressed_count();
    test_filtered_recovery_still_closes_episode();
    test_repeat_budget_limits_repeats();
    test_first_error_and_recovery_bypass_exhausted_budget();
    test_summary_requires_activity();
    test_summary_reports_active_domains_once();
    test_poll_combines_recovery_and_summary();
}
