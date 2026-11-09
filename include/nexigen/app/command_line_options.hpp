// nexigen::app — parsed command line (spec app/command-line).
#pragma once
#include <optional>
#include <string>
#include <nlohmann/json.hpp>
#include <nexigen/core/log_level.hpp>
namespace nexigen::app {
struct CommandLineOptions {
    std::string global_settings_dir{};
    std::string local_settings_dir{};
    std::string app_name{};
    std::optional<core::LogLevel> log_level{};
    bool help{false};
    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const CommandLineOptions& options) {
    return options.to_string();
}
void to_json(nlohmann::json& j, const CommandLineOptions& options);
void from_json(const nlohmann::json& j, CommandLineOptions& options) noexcept;
} // namespace nexigen::app
