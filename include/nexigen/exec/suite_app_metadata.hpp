// nexigen::exec — one application record inside suite.json
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::exec {

/// One application record inside suite.json.
struct SuiteAppMetadata {
    std::string app{};
    std::string instance{};
    std::string instance_dir{};
    std::uint64_t stop_grace_s{5};

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] inline std::string to_string(const SuiteAppMetadata& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const SuiteAppMetadata& app);
void from_json(const nlohmann::json& j, SuiteAppMetadata& app) noexcept;

} // namespace nexigen::exec
