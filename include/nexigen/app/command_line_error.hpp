// nexigen::app — failure payload for command-line parsing (spec app/command-line).
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <nexigen/app/command_line_diagnostic.hpp>
#include <nexigen/core/result.hpp>
namespace nexigen::app {
struct CommandLineError {
    core::ErrorCode code{core::ErrorCode::InvalidArgument};
    std::vector<CommandLineDiagnostic> diagnostics{};
    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const CommandLineError& error) {
    return error.to_string();
}
void to_json(nlohmann::json& j, const CommandLineError& error);
void from_json(const nlohmann::json& j, CommandLineError& error) noexcept;
} // namespace nexigen::app
