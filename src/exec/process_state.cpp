// nexigen::exec — ProcessState JSON serialization
// (change align-exec-process-model-with-autosar; spec exec/process-types).
#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/exec/process_state.hpp>

namespace nexigen::exec {

void to_json(nlohmann::json& j, ProcessState value) {
    core::mandate::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, ProcessState& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::exec
