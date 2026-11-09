// nexigen::exec — AppState JSON serialization
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#include <nlohmann/json.hpp>

#include <nexigen/exec/app_state.hpp>
#include <nexigen/core/enum_traits.hpp>

namespace nexigen::exec {

void to_json(nlohmann::json& j, AppState value) {
    core::mandate::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, AppState& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::exec
