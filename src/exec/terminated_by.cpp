// nexigen::exec — TerminatedBy JSON serialization
// (change refactor-execution_manager-structure; spec execution_manager/instance-lifecycle).
#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/exec/terminated_by.hpp>

namespace nexigen::exec {

void to_json(nlohmann::json& j, TerminatedBy value) {
    core::mandate::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, TerminatedBy& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::exec
