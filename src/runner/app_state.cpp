// nexigen::runner — AppState JSON serialization
// (change refactor-runner-structure; spec runner/app-suite).
#include <nlohmann/json.hpp>

#include <nexigen/runner/app_state.hpp>
#include <nexigen/runner/detail/sys_helpers.hpp>

namespace nexigen::runner {

void to_json(nlohmann::json& j, AppState value) {
    detail::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, AppState& value) noexcept {
    detail::enum_from_json(j, value);
}

} // namespace nexigen::runner
