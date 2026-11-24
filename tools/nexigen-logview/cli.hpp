// nexigen-logview — command-line parsing.
#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::logview {

/// @brief Parsed command line.
struct CliOptions {
    std::string config_path{};     ///< `--config`; empty means the default path.
    bool config_given{false};      ///< Whether `--config` was supplied.
    std::string service_name{};    ///< `--service`; empty keeps the configuration.
    bool service_given{false};     ///< Whether `--service` was supplied.
    std::string offline_file{};    ///< `--offline` file.
    bool offline{false};           ///< Whether offline mode was requested.
    std::uint32_t memory_mb{0};    ///< `--memory-mb`; zero keeps the configuration.
    bool memory_given{false};      ///< Whether `--memory-mb` was supplied.
    bool headless{false};          ///< Dump records without a window.
    bool ui_smoke{false};          ///< Open one window frame and exit (CI smoke).
    bool ui_actions_test{false};   ///< Exercise copy/truncation/open actions under Xvfb.
    bool ui_input_test{false};     ///< Drive scripted ImGui clicks under Xvfb.
    bool ui_view_test{false};      ///< Exercise multi-view docking, clone, and limits.
    bool ui_focus_test{false};     ///< Exercise keyboard focus routing under Xvfb.
    bool ui_splash_test{false};    ///< Render the startup splash and exit.
    float ui_scale{0.0F};          ///< Explicit UI scale; zero means auto-detect.
    std::uint64_t mock_records{0}; ///< Synthetic records for `--headless`.
    std::uint64_t max_records{0};  ///< Stop after this many headless records.
    std::uint64_t soak_seconds{0}; ///< Headless sustained-rate soak duration.
    bool help{false};              ///< `--help` was requested.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the selected mode and overrides.
    [[nodiscard]] std::string to_string() const {
        return std::format("CliOptions(config={}, service={}, offline={}, memory_mb={}, "
                           "headless={}, mock={}, soak={}, smoke={}, scale={:.2f})",
                           config_path, service_name, offline, memory_mb, headless, mock_records,
                           soak_seconds, ui_smoke, ui_scale);
    }
};

/// @brief Usage text.
/// @return Multi-line help.
[[nodiscard]] std::string_view usage_text() noexcept;

/// @brief Parse the command line.
/// @param argc Argument count.
/// @param argv Argument vector.
/// @return Parsed options or `InvalidArgument` on a malformed command line.
[[nodiscard]] nexigen::core::Result<CliOptions> parse_cli(int argc, char* argv[]);

} // namespace nexigen::logview
