// nexigen::exec — SuiteState JSON serialization
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/exec/suite_state.hpp>

namespace nexigen::exec {

void to_json(nlohmann::json& j, SuiteState value) {
    core::mandate::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, SuiteState& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::exec
