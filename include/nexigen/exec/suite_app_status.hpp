// nexigen::exec — one application state in `suite-state.json` and `status`
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/exec/app_state.hpp>

namespace nexigen::exec {

/// One application state in `suite-state.json` and `status`.
struct SuiteAppStatus {
    std::string app{};
    std::string instance{};
    std::string instance_dir{};
    std::string artifact_dir{};
    std::uint32_t attempt{0};
    AppState state{AppState::Unknown};
    std::uint32_t restarts_used{0};

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] inline std::string to_string(const SuiteAppStatus& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const SuiteAppStatus& status);
void from_json(const nlohmann::json& j, SuiteAppStatus& status) noexcept;

} // namespace nexigen::exec
