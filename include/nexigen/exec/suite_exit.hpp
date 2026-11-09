// nexigen::exec — final suite record (`suite-exit.json`)
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/exec/suite_state.hpp>

namespace nexigen::exec {

/// Final suite record (`suite-exit.json`).
struct SuiteExit {
    SuiteState state{SuiteState::Unknown};
    std::string reason{};
    std::uint64_t uptime_s{0};

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] inline std::string to_string(const SuiteExit& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const SuiteExit& exit);
void from_json(const nlohmann::json& j, SuiteExit& exit) noexcept;

} // namespace nexigen::exec
