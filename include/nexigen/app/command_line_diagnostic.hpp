// nexigen::app — one command-line finding (spec app/command-line).
#pragma once
#include <string>
#include <nlohmann/json.hpp>
namespace nexigen::app {
struct CommandLineDiagnostic {
    std::string option{};
    std::string message{};
    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const CommandLineDiagnostic& diagnostic) {
    return diagnostic.to_string();
}
void to_json(nlohmann::json& j, const CommandLineDiagnostic& diagnostic);
void from_json(const nlohmann::json& j, CommandLineDiagnostic& diagnostic) noexcept;
} // namespace nexigen::app
