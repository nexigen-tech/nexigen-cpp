// nexigen::exec — initial MachineFG transition result query payloads
// (change machinefg-as-em-function-group).
#include <nexigen/exec/machine_state_query.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::exec {

std::string InitialMachineStateTransitionResultResponse::to_string() const {
    return std::format("InitialMachineStateTransitionResultResponse(result={})",
                       nexigen::exec::to_string(result));
}

void to_json(nlohmann::json& j, const InitialMachineStateTransitionResultResponse& response) {
    j = nlohmann::json{{"result", nexigen::exec::to_string(response.result)}};
}

void from_json(const nlohmann::json& j,
               InitialMachineStateTransitionResultResponse& response) noexcept {
    std::string result;
    if (!core::detail::read_string(j, "result", result)) {
        return;
    }
    StateResult parsed = response.result;
    if (from_string(result, parsed)) {
        response.result = parsed;
    }
}

} // namespace nexigen::exec
