// nexigen::runner — TerminatedBy JSON serialization
// (change refactor-runner-structure; spec runner/instance-lifecycle).
#include <nlohmann/json.hpp>

#include <nexigen/runner/detail/sys_helpers.hpp>
#include <nexigen/runner/terminated_by.hpp>

namespace nexigen::runner {

void to_json(nlohmann::json& j, TerminatedBy value) {
    detail::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, TerminatedBy& value) noexcept {
    detail::enum_from_json(j, value);
}

} // namespace nexigen::runner
