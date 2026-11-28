// Cold-path logging and core-purity tests (change add-canopen-observability;
// spec canopen/observability: "Cold-path logging is detailed and bounded",
// "CANopen core remains telemetry-free").
#include "observability_test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <nexigen/canopen/eds_dictionary.hpp>
#include <nexigen/canopen/eds_parser.hpp>
#include <nexigen/canopen/nmt_state.hpp>
#include <nexigen/canopen/observability/can_open_log_policy.hpp>
#include <nexigen/canopen/observability/can_open_runtime_state.hpp>
#include <nexigen/canopen/observability/cold_path_logging.hpp>
#include <nexigen/core/log_level.hpp>

namespace {

namespace canopen = nexigen::canopen;
namespace obs = nexigen::canopen::observability;

/// @brief Minimal valid EDS with one object and one transmit PDO mapping.
constexpr std::string_view kEds = R"([1000]
ParameterName=DeviceType
DataType=0x0007
[1A00]
ObjectType=0x9
SubNumber=2
[1A00sub1]
DefaultValue=0x20000020
[2000]
ParameterName=DrivePosition
DataType=0x0007
)";

/// @brief Build a failed parse result with `count` diagnostics.
/// @param count Number of diagnostics.
/// @return Result whose dictionary is null.
canopen::EdsParseResult failed_result(std::size_t count) {
    canopen::EdsParseResult result{};
    for (std::size_t index = 0U; index < count; ++index) {
        result.diagnostics.push_back(
            canopen::EdsDiagnostic{index + 1U, std::format("bad line {}", index + 1U)});
    }
    return result;
}

// --- 5.1 EDS result logging --------------------------------------------------

void test_eds_success_reports_path_hash_and_counts() {
    auto parsed = canopen::EdsParser::parse(kEds, "drive.eds");
    NX_CHECK(parsed.ok());
    if (!parsed.ok()) {
        return;
    }
    CaptureLogger logger;
    clear_captured_records();
    obs::log_eds_result(logger, parsed, "can0", "drive.eds");
    NX_CHECK_EQ(captured_records().size(), std::size_t{1});
    NX_CHECK(captured_contains("canopen eds loaded"));
    NX_CHECK(captured_contains("bus=can0"));
    NX_CHECK(captured_contains("path=drive.eds"));
    NX_CHECK(captured_contains(std::format("hash=0x{:016x}", parsed.dictionary->content_hash())));
    NX_CHECK(captured_contains(std::format("objects={}", parsed.dictionary->object_count())));
    NX_CHECK(
        captured_contains(std::format("pdo_mappings={}", parsed.dictionary->mappings().size())));
    NX_CHECK_EQ(captured_level_count(nexigen::core::LogLevel::Info), std::size_t{1});
}

void test_eds_failure_is_capped_with_omitted_trailer() {
    const auto result = failed_result(40U);
    CaptureLogger logger;
    obs::CanOpenLogPolicy policy{};
    NX_CHECK_EQ(policy.max_cold_diagnostics, 32U);
    clear_captured_records();
    obs::log_eds_result(logger, result, "can1", "broken.eds", policy);
    NX_CHECK_EQ(captured_records().size(), std::size_t{34});
    NX_CHECK(captured_contains("canopen eds load failed"));
    NX_CHECK(captured_contains("diagnostics=40"));
    NX_CHECK_EQ(captured_count("canopen eds diagnostic bus="), std::size_t{32});
    NX_CHECK(captured_contains("bad line 32"));
    NX_CHECK(!captured_contains("bad line 33"));
    NX_CHECK_EQ(captured_count("canopen eds diagnostics omitted"), std::size_t{1});
    NX_CHECK(captured_contains("omitted=8"));
    NX_CHECK_EQ(captured_level_count(nexigen::core::LogLevel::Error), std::size_t{1});
    NX_CHECK_EQ(captured_level_count(nexigen::core::LogLevel::Warning), std::size_t{33});
}

void test_eds_failure_without_diagnostics_logs_only_the_summary() {
    const auto result = failed_result(0U);
    CaptureLogger logger;
    clear_captured_records();
    obs::log_eds_result(logger, result, "can0", "empty.eds");
    NX_CHECK_EQ(captured_records().size(), std::size_t{1});
    NX_CHECK(captured_contains("canopen eds load failed"));
    NX_CHECK(captured_contains("diagnostics=0"));
    NX_CHECK(!captured_contains("omitted="));
}

void test_eds_zero_cap_logs_only_the_trailer() {
    const auto result = failed_result(3U);
    CaptureLogger logger;
    obs::CanOpenLogPolicy policy{};
    policy.max_cold_diagnostics = 0U;
    clear_captured_records();
    obs::log_eds_result(logger, result, "can0", "capped.eds", policy);
    NX_CHECK_EQ(captured_records().size(), std::size_t{2});
    NX_CHECK_EQ(captured_count("canopen eds diagnostic bus="), std::size_t{0});
    NX_CHECK(captured_contains("omitted=3"));
}

// --- 5.2 lifecycle and NMT transitions --------------------------------------

void test_runtime_transition_is_info_with_bus_and_states() {
    CaptureLogger logger;
    clear_captured_records();
    obs::log_runtime_transition(logger, "can0", obs::CanOpenRuntimeState::Stopped,
                                obs::CanOpenRuntimeState::Running);
    NX_CHECK_EQ(captured_records().size(), std::size_t{1});
    NX_CHECK_EQ(captured_level_count(nexigen::core::LogLevel::Info), std::size_t{1});
    NX_CHECK(captured_contains("canopen runtime transition"));
    NX_CHECK(captured_contains("bus=can0"));
    NX_CHECK(captured_contains("from=Stopped"));
    NX_CHECK(captured_contains("to=Running"));
}

void test_nmt_transition_is_info_with_bus_node_and_states() {
    CaptureLogger logger;
    clear_captured_records();
    obs::log_nmt_transition(logger, "can1", 5U, canopen::NmtState::PreOperational,
                            canopen::NmtState::Operational);
    NX_CHECK_EQ(captured_records().size(), std::size_t{1});
    NX_CHECK_EQ(captured_level_count(nexigen::core::LogLevel::Info), std::size_t{1});
    NX_CHECK(captured_contains("canopen nmt transition"));
    NX_CHECK(captured_contains("bus=can1"));
    NX_CHECK(captured_contains("node=5"));
    NX_CHECK(captured_contains("from=PreOperational"));
    NX_CHECK(captured_contains("to=Operational"));
}

// --- 5.3 core purity ---------------------------------------------------------

/// @brief Whether a path belongs to the observability layer.
/// @param path File path.
/// @return `true` when any component is `observability`.
bool is_observability_path(const std::filesystem::path& path) {
    for (const std::filesystem::path& part : path) {
        if (part == "observability") {
            return true;
        }
    }
    return false;
}

/// @brief Scan one core source tree for telemetry includes.
/// @param root Directory to walk.
void scan_core_tree(const std::filesystem::path& root) {
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
        if (!entry.is_regular_file() || is_observability_path(entry.path())) {
            continue;
        }
        std::ifstream stream{entry.path()};
        std::ostringstream text;
        text << stream.rdbuf();
        const std::string content = text.str();
        NX_CHECK(content.find("#include <nexigen/logging/") == std::string::npos);
        NX_CHECK(content.find("#include <nexigen/metrics/") == std::string::npos);
        NX_CHECK(content.find("#include <nexigen/tracing/") == std::string::npos);
        NX_CHECK(content.find("#include <nexigen/com/") == std::string::npos);
        NX_CHECK(content.find("#include <nexigen/phm/") == std::string::npos);
    }
    NX_CHECK(!error);
}

void test_core_sources_do_not_include_telemetry() {
    const std::filesystem::path root{NEXIGEN_SOURCE_DIR};
    scan_core_tree(root / "include" / "nexigen" / "canopen");
    scan_core_tree(root / "src" / "canopen");
}

void test_core_target_does_not_link_telemetry() {
    std::ifstream stream{std::filesystem::path{NEXIGEN_SOURCE_DIR} / "CMakeLists.txt"};
    NX_CHECK(stream.good());
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const std::string cmake = buffer.str();
    const std::string marker = "target_link_libraries(nexigen_canopen PUBLIC";
    const std::size_t start = cmake.find(marker);
    NX_CHECK(start != std::string::npos);
    if (start == std::string::npos) {
        return;
    }
    const std::size_t end = cmake.find(')', start);
    NX_CHECK(end != std::string::npos);
    const std::string dependencies = cmake.substr(start, end - start);
    NX_CHECK(dependencies.find("nexigen_core") != std::string::npos);
    NX_CHECK(dependencies.find("nexigen_logging") == std::string::npos);
    NX_CHECK(dependencies.find("nexigen_metrics") == std::string::npos);
    NX_CHECK(dependencies.find("nexigen_tracing") == std::string::npos);
}

} // namespace

void suite_cold_path() {
    test_eds_success_reports_path_hash_and_counts();
    test_eds_failure_is_capped_with_omitted_trailer();
    test_eds_failure_without_diagnostics_logs_only_the_summary();
    test_eds_zero_cap_logs_only_the_trailer();
    test_runtime_transition_is_info_with_bus_and_states();
    test_nmt_transition_is_info_with_bus_node_and_states();
    test_core_sources_do_not_include_telemetry();
    test_core_target_does_not_link_telemetry();
}
