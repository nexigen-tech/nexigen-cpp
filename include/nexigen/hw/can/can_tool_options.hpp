#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_channel_config.hpp>

namespace nexigen::hw::can {

/// @brief Parsed command-line options shared by the CAN tools.
struct CanToolOptions {
    std::vector<CanChannelConfig> channels{}; ///< Configured channels.
    std::string app_id{};                     ///< Required application id.
    std::string app_name{};                   ///< Long app name for telemetry.
    std::string backend{"hardware"};          ///< `hardware` or `virtual`.
    bool embedded{false};                     ///< Use an embedded time base.
    bool help{false};                         ///< `--help` was requested.
    bool json{false};                         ///< Emit JSONL from the monitor.
    bool errors_only{false};                  ///< Monitor only error messages.
    std::string filter_channel{};             ///< Monitor channel filter ("" = all).
    std::uint32_t filter_id{0};               ///< Monitor identifier filter.
    bool has_filter_id{false};                ///< Whether `filter_id` is active.
    std::string filter_kind{};                ///< Monitor kind filter ("" = all).
    std::string filter_origin{};              ///< Monitor origin filter ("" = all).
    std::uint64_t seed{0};                    ///< Virtual-bus deterministic seed.
    bool has_seed{false};                     ///< Whether `seed` was supplied.
    std::uint64_t duration_ms{0};             ///< Virtual-bus run duration.
    std::uint64_t latency_ns{0};              ///< Virtual loopback latency.
    std::uint64_t jitter_ns{0};               ///< Virtual loopback jitter.
    bool external{false};                     ///< Inject external frames.
    bool inject_errors{false};                ///< Inject error frames.
    bool inject_overload{false};              ///< Inject overload frames.
    bool inject_bus_off{false};               ///< Inject a bus-off error.
    bool inject_warning{false};               ///< Inject an error-warning frame.
    bool inject_passive{false};               ///< Inject an error-passive frame.
    bool inject_drop{false};                  ///< Drop occasional loopback frames.
    bool inject_duplicate{false};             ///< Duplicate occasional loopback frames.
    bool inject_reorder{false};               ///< Reorder loopback frames pairwise.
    bool inject_burst{false};                 ///< Emit a three-frame burst.
};

/// @brief Parse CAN tool options without consuming App identity options.
/// @param argc Argument count from `main`.
/// @param argv Argument vector from `main`.
/// @return Parsed options, or `InvalidArgument` with a diagnostic.
[[nodiscard]] core::Result<CanToolOptions> parse_tool_options(int argc,
                                                              const char* const argv[]) noexcept;

/// @brief Usage text for the CAN tools.
/// @return Static usage string.
[[nodiscard]] std::string_view can_tool_usage() noexcept;

} // namespace nexigen::hw::can
