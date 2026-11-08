// nexigen::runner — TerminatedBy JSON serialization
// (change refactor-runner-structure; spec runner/instance-lifecycle).
#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/runner/terminated_by.hpp>

namespace nexigen::runner {

void to_json(nlohmann::json& j, TerminatedBy value) {
    core::mandate::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, TerminatedBy& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::runner
