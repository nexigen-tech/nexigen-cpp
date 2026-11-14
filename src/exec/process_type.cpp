// nexigen::exec — ProcessType JSON serialization
// (change introduce-platformfg-process-model; spec exec/process-types).
#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/exec/process_type.hpp>

namespace nexigen::exec {

void to_json(nlohmann::json& j, ProcessType value) {
    core::mandate::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, ProcessType& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::exec
