// nexigen::exec — StateResult JSON serialization
// (change align-exec-process-model-with-autosar; spec exec/state-client).
#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/exec/state_result.hpp>

namespace nexigen::exec {

void to_json(nlohmann::json& j, StateResult value) {
    core::mandate::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, StateResult& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::exec
