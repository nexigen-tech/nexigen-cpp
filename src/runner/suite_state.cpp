// nexigen::runner — SuiteState JSON serialization
// (change refactor-runner-structure; spec runner/app-suite).
#include <nlohmann/json.hpp>

#include <nexigen/runner/detail/sys_helpers.hpp>
#include <nexigen/runner/suite_state.hpp>

namespace nexigen::runner {

void to_json(nlohmann::json& j, SuiteState value) {
    detail::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, SuiteState& value) noexcept {
    detail::enum_from_json(j, value);
}

} // namespace nexigen::runner
