// nexigen::runner — AppState JSON serialization
// (change refactor-runner-structure; spec runner/app-suite).
#include <nlohmann/json.hpp>

#include <nexigen/runner/app_state.hpp>
#include <nexigen/core/enum_traits.hpp>

namespace nexigen::runner {

void to_json(nlohmann::json& j, AppState value) {
    core::mandate::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, AppState& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::runner
