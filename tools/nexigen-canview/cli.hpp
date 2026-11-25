// nexigen-canview — command-line parsing.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/core/result.hpp>

#include "delta_mode.hpp"
#include "session_record.hpp"
#include "time_column_mode.hpp"
#include "timestamp_mode.hpp"
#include "view_channel.hpp"

namespace nexigen::canview {

/// @brief Parsed command line.
struct CliOptions {
    std::string config_path{};                           ///< `--config`; empty means the XDG path.
    bool config_given{false};                            ///< Whether `--config` was supplied.
    std::vector<CanViewChannel> channels{};              ///< `--channel name[:id]` values.
    std::vector<SessionEdsBinding> eds{};                ///< `--eds channel:node:path` values.
    std::uint32_t view_count{1U};                        ///< Number of LIVE All views.
    bool views_given{false};                             ///< Whether `--views` was supplied.
    std::uint32_t memory_mb{0};                          ///< `--memory-mb`; zero keeps the config.
    bool memory_given{false};                            ///< Whether `--memory-mb` was supplied.
    bool shared_timebase{false};                         ///< `--shared-timebase`.
    bool shared_given{false};                            ///< Whether the flag was supplied.
    TimeColumnMode time_columns{TimeColumnMode::HwOnly}; ///< `--time-columns`.
    bool columns_given{false};                           ///< Whether the flag was supplied.
    TimestampMode timestamp_mode{TimestampMode::Driver}; ///< `--timestamp-mode`.
    bool timestamp_given{false};                         ///< Whether the flag was supplied.
    DeltaMode delta_mode{DeltaMode::PreviousVisible};    ///< `--delta-mode`.
    bool delta_given{false};                             ///< Whether the flag was supplied.
    bool errors_only{false};                             ///< `--errors-only`.
    bool show_bus_idle{false};                           ///< `--show-bus-idle`.
    std::string filter_text{};                           ///< `--filter-text`.
    std::uint32_t filter_id{0};                          ///< `--filter-id`.
    std::uint32_t filter_mask{0x7FFU};                   ///< `--filter-mask`.
    bool filter_id_given{false};                         ///< Whether an ID filter was supplied.
    bool offline{false};                                 ///< `--offline` was requested.
    std::string offline_file{};                          ///< Offline session path.
    bool headless{false};                                ///< `--headless`.
    bool json{false};                                    ///< `--json`.
    std::uint64_t max_records{0};                        ///< `--max-records`; zero means unlimited.
    std::uint64_t mock_records{0};                       ///< `--mock` synthetic records.
    std::uint64_t soak_seconds{0};                       ///< `--soak` duration.
    std::uint64_t mock_rate{50'000};                     ///< `--rate` synthetic frames/s.
    bool ui_smoke{false};                                ///< `--ui-smoke`.
    bool ui_actions_test{false};                         ///< `--ui-actions-test`.
    bool ui_input_test{false};                           ///< `--ui-input-test`.
    bool ui_view_test{false};                            ///< `--ui-view-test`.
    bool ui_focus_test{false};                           ///< `--ui-focus-test`.
    bool ui_splash_test{false};                          ///< `--ui-splash-test`.
    float ui_scale{0.0F};                                ///< `--ui-scale`; zero means auto.
    bool help{false};                                    ///< `--help`.
    bool version{false};                                 ///< `--version`.

    /// @brief Whether any scripted UI mode was requested.
    /// @return `true` for any `--ui-*-test` mode.
    [[nodiscard]] bool ui_test_mode() const noexcept {
        return ui_smoke || ui_actions_test || ui_input_test || ui_view_test || ui_focus_test ||
               ui_splash_test;
    }

    /// @brief Cold-path diagnostic rendering.
    /// @return Selected options.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Usage text.
/// @return Multi-line help.
[[nodiscard]] std::string_view usage_text() noexcept;

/// @brief Parse the command line.
/// @param argc Argument count.
/// @param argv Argument vector.
/// @return Parsed options or `InvalidArgument` on a malformed command line.
[[nodiscard]] nexigen::core::Result<CliOptions> parse_cli(int argc, char* argv[]);

} // namespace nexigen::canview
