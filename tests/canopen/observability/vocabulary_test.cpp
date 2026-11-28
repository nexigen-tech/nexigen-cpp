// Vocabulary tests for the CANopen observability layer (change
// add-canopen-observability; spec canopen/observability): runtime state, error
// domain, and log policy serialization/validation/formatter parity.
#include "observability_test_support.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/canopen/observability/can_open_error_domain.hpp>
#include <nexigen/canopen/observability/can_open_log_policy.hpp>
#include <nexigen/canopen/observability/can_open_runtime_state.hpp>

namespace {

namespace obs = nexigen::canopen::observability;

using obs::CanOpenErrorDomain;
using obs::CanOpenLogPolicy;
using obs::CanOpenRuntimeState;

/// @brief Compare every log-policy field.
/// @param left First policy.
/// @param right Second policy.
/// @return `true` when all nine fields match.
bool same_policy(const CanOpenLogPolicy& left, const CanOpenLogPolicy& right) {
    return left.repeat_interval_ms == right.repeat_interval_ms &&
           left.recovery_hold_ms == right.recovery_hold_ms &&
           left.max_suppressed_per_key == right.max_suppressed_per_key &&
           left.summary_interval_ms == right.summary_interval_ms &&
           left.max_hot_logs_per_second == right.max_hot_logs_per_second &&
           left.hot_error_level == right.hot_error_level &&
           left.recovery_level == right.recovery_level &&
           left.summary_level == right.summary_level &&
           left.max_cold_diagnostics == right.max_cold_diagnostics;
}

// --- runtime state -----------------------------------------------------------

void test_runtime_state_names() {
    constexpr std::array<CanOpenRuntimeState, 5> states{
        CanOpenRuntimeState::Stopped, CanOpenRuntimeState::Starting, CanOpenRuntimeState::Running,
        CanOpenRuntimeState::Degraded, CanOpenRuntimeState::Error};
    constexpr std::array<std::string_view, 5> names{"Stopped", "Starting", "Running", "Degraded",
                                                    "Error"};
    for (std::size_t index = 0U; index < states.size(); ++index) {
        NX_CHECK_EQ(obs::to_string(states[index]), names[index]);
        CanOpenRuntimeState parsed = CanOpenRuntimeState::Error;
        NX_CHECK(obs::from_string(names[index], parsed));
        NX_CHECK(parsed == states[index]);
    }
}

void test_runtime_state_parse_rejects_unknown() {
    CanOpenRuntimeState state = CanOpenRuntimeState::Running;
    NX_CHECK(!obs::from_string("running", state));
    NX_CHECK(state == CanOpenRuntimeState::Running);
    NX_CHECK(!obs::from_string("", state));
    NX_CHECK(state == CanOpenRuntimeState::Running);
}

void test_runtime_state_json() {
    for (const CanOpenRuntimeState state :
         {CanOpenRuntimeState::Stopped, CanOpenRuntimeState::Running, CanOpenRuntimeState::Error}) {
        nlohmann::json document = state;
        NX_CHECK_EQ(document.get<std::string>(), obs::to_string(state));
        NX_CHECK(document.get<CanOpenRuntimeState>() == state);
    }
    CanOpenRuntimeState state = CanOpenRuntimeState::Degraded;
    obs::from_json(nlohmann::json(7), state);
    NX_CHECK(state == CanOpenRuntimeState::Degraded);
    obs::from_json(nlohmann::json("NotAState"), state);
    NX_CHECK(state == CanOpenRuntimeState::Degraded);
    obs::from_json(nlohmann::json{}, state);
    NX_CHECK(state == CanOpenRuntimeState::Degraded);
}

void test_runtime_state_formatter() {
    NX_CHECK_EQ(std::format("{}", CanOpenRuntimeState::Running),
                std::string(obs::to_string(CanOpenRuntimeState::Running)));
    NX_CHECK_EQ(std::format("[{:>10}]", CanOpenRuntimeState::Error), std::string("[     Error]"));
}

// --- error domain ------------------------------------------------------------

void test_error_domain_names() {
    constexpr std::array<CanOpenErrorDomain, 9> domains{
        CanOpenErrorDomain::Bus,    CanOpenErrorDomain::Transport, CanOpenErrorDomain::Nmt,
        CanOpenErrorDomain::Sdo,    CanOpenErrorDomain::Pdo,       CanOpenErrorDomain::Emcy,
        CanOpenErrorDomain::Signal, CanOpenErrorDomain::Ipc,       CanOpenErrorDomain::Config};
    constexpr std::array<std::string_view, 9> names{"bus",  "transport", "nmt", "sdo",   "pdo",
                                                    "emcy", "signal",    "ipc", "config"};
    for (std::size_t index = 0U; index < domains.size(); ++index) {
        NX_CHECK_EQ(static_cast<std::size_t>(domains[index]), index);
        NX_CHECK_EQ(obs::to_string(domains[index]), names[index]);
        CanOpenErrorDomain parsed = CanOpenErrorDomain::Config;
        NX_CHECK(obs::from_string(names[index], parsed));
        NX_CHECK(parsed == domains[index]);
    }
    NX_CHECK_EQ(obs::error_domain_count, domains.size());
}

void test_error_domain_parse_rejects_case_and_unknown() {
    CanOpenErrorDomain domain = CanOpenErrorDomain::Pdo;
    NX_CHECK(!obs::from_string("Pdo", domain));
    NX_CHECK(domain == CanOpenErrorDomain::Pdo);
    NX_CHECK(!obs::from_string("unknown-domain", domain));
    NX_CHECK(domain == CanOpenErrorDomain::Pdo);
}

void test_error_domain_json_and_format() {
    for (const CanOpenErrorDomain domain :
         {CanOpenErrorDomain::Bus, CanOpenErrorDomain::Signal, CanOpenErrorDomain::Config}) {
        nlohmann::json document = domain;
        NX_CHECK_EQ(document.get<std::string>(), obs::to_string(domain));
        NX_CHECK(document.get<CanOpenErrorDomain>() == domain);
        NX_CHECK_EQ(std::format("{}", domain), std::string(obs::to_string(domain)));
    }
    CanOpenErrorDomain domain = CanOpenErrorDomain::Emcy;
    obs::from_json(nlohmann::json(3), domain);
    NX_CHECK(domain == CanOpenErrorDomain::Emcy);
    obs::from_json(nlohmann::json("EMCY"), domain);
    NX_CHECK(domain == CanOpenErrorDomain::Emcy);
}

// --- log policy --------------------------------------------------------------

void test_policy_defaults() {
    const CanOpenLogPolicy policy{};
    NX_CHECK_EQ(policy.repeat_interval_ms, 5'000U);
    NX_CHECK_EQ(policy.recovery_hold_ms, 5'000U);
    NX_CHECK_EQ(policy.max_suppressed_per_key, 10'000U);
    NX_CHECK_EQ(policy.summary_interval_ms, 60'000U);
    NX_CHECK_EQ(policy.max_hot_logs_per_second, 50U);
    NX_CHECK(policy.hot_error_level == nexigen::core::LogLevel::Error);
    NX_CHECK(policy.recovery_level == nexigen::core::LogLevel::Info);
    NX_CHECK(policy.summary_level == nexigen::core::LogLevel::Info);
    NX_CHECK_EQ(policy.max_cold_diagnostics, 32U);
    NX_CHECK(policy.validate().has_value());
}

void test_policy_range_rejection() {
    CanOpenLogPolicy invalid{};
    invalid.repeat_interval_ms = 0U;
    NX_CHECK(!invalid.validate().has_value());

    invalid = CanOpenLogPolicy{};
    invalid.recovery_hold_ms = CanOpenLogPolicy::max_recovery_hold_ms + 1U;
    NX_CHECK(!invalid.validate().has_value());

    invalid = CanOpenLogPolicy{};
    invalid.max_suppressed_per_key = 0U;
    NX_CHECK(!invalid.validate().has_value());

    invalid = CanOpenLogPolicy{};
    invalid.summary_interval_ms = CanOpenLogPolicy::min_summary_interval_ms - 1U;
    NX_CHECK(!invalid.validate().has_value());

    invalid = CanOpenLogPolicy{};
    invalid.max_hot_logs_per_second = 0U;
    NX_CHECK(!invalid.validate().has_value());

    invalid = CanOpenLogPolicy{};
    invalid.max_cold_diagnostics = CanOpenLogPolicy::max_cold_diagnostics_limit + 1U;
    NX_CHECK(!invalid.validate().has_value());

    CanOpenLogPolicy boundary{};
    boundary.repeat_interval_ms = CanOpenLogPolicy::min_repeat_interval_ms;
    boundary.recovery_hold_ms = CanOpenLogPolicy::max_recovery_hold_ms;
    boundary.max_suppressed_per_key = CanOpenLogPolicy::max_suppressed_per_key_limit;
    boundary.summary_interval_ms = CanOpenLogPolicy::min_summary_interval_ms;
    boundary.max_hot_logs_per_second = CanOpenLogPolicy::max_hot_logs_per_second_limit;
    boundary.max_cold_diagnostics = CanOpenLogPolicy::max_cold_diagnostics_limit;
    NX_CHECK(boundary.validate().has_value());
}

void test_policy_off_levels_are_valid() {
    CanOpenLogPolicy policy{};
    policy.hot_error_level = nexigen::core::LogLevel::Off;
    policy.recovery_level = nexigen::core::LogLevel::Off;
    policy.summary_level = nexigen::core::LogLevel::Off;
    NX_CHECK(policy.validate().has_value());
}

void test_policy_json_round_trip() {
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 1'500U;
    policy.recovery_hold_ms = 2'500U;
    policy.max_suppressed_per_key = 77U;
    policy.summary_interval_ms = 9'000U;
    policy.max_hot_logs_per_second = 7U;
    policy.hot_error_level = nexigen::core::LogLevel::Warning;
    policy.recovery_level = nexigen::core::LogLevel::Debug;
    policy.summary_level = nexigen::core::LogLevel::Fatal;
    policy.max_cold_diagnostics = 3U;

    const nlohmann::json document = policy;
    NX_CHECK_EQ(document.at("hot_error_level").get<std::string>(), "warning");
    const CanOpenLogPolicy parsed = document.get<CanOpenLogPolicy>();
    NX_CHECK(same_policy(parsed, policy));
}

void test_policy_json_tolerance() {
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 1'234U;
    policy.max_cold_diagnostics = 9U;

    nlohmann::json document{
        {"repeat_interval_ms", "not-a-number"},
        {"max_cold_diagnostics", 0U},
    };
    obs::from_json(document, policy);
    NX_CHECK_EQ(policy.repeat_interval_ms, 1'234U);
    NX_CHECK_EQ(policy.max_cold_diagnostics, 0U);
    NX_CHECK_EQ(policy.recovery_hold_ms, 5'000U);
}

void test_policy_json_out_of_range_keeps_field() {
    CanOpenLogPolicy policy{};
    nlohmann::json document{
        {"repeat_interval_ms", 0U},
        {"recovery_hold_ms", -5},
        {"summary_interval_ms", 999U},
        {"max_cold_diagnostics", 2'000U},
    };
    obs::from_json(document, policy);
    NX_CHECK_EQ(policy.repeat_interval_ms, 5'000U);
    NX_CHECK_EQ(policy.recovery_hold_ms, 5'000U);
    NX_CHECK_EQ(policy.summary_interval_ms, 60'000U);
    NX_CHECK_EQ(policy.max_cold_diagnostics, 32U);
}

void test_policy_json_non_object_is_ignored() {
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 4'321U;
    obs::from_json(nlohmann::json::array(), policy);
    NX_CHECK_EQ(policy.repeat_interval_ms, 4'321U);
}

void test_policy_formatter_matches_to_string() {
    CanOpenLogPolicy policy{};
    policy.repeat_interval_ms = 1'111U;
    NX_CHECK_EQ(std::format("{}", policy), policy.to_string());
    NX_CHECK(policy.to_string().find("repeat_interval_ms=1111") != std::string::npos);
    NX_CHECK(policy.to_string().find("hot_error_level=error") != std::string::npos);
}

} // namespace

void suite_vocabulary() {
    test_runtime_state_names();
    test_runtime_state_parse_rejects_unknown();
    test_runtime_state_json();
    test_runtime_state_formatter();
    test_error_domain_names();
    test_error_domain_parse_rejects_case_and_unknown();
    test_error_domain_json_and_format();
    test_policy_defaults();
    test_policy_range_rejection();
    test_policy_off_levels_are_valid();
    test_policy_json_round_trip();
    test_policy_json_tolerance();
    test_policy_json_out_of_range_keeps_field();
    test_policy_json_non_object_is_ignored();
    test_policy_formatter_matches_to_string();
}
