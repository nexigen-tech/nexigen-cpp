// nexigen::exec — initial RootFG transition result query payloads
// (change machinefg-as-em-function-group).
#include <nexigen/exec/root_state_query.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::exec {

std::string InitialRootStateTransitionResultResponse::to_string() const {
    return std::format("InitialRootStateTransitionResultResponse(result={})",
                       nexigen::exec::to_string(result));
}

void to_json(nlohmann::json& j, const InitialRootStateTransitionResultResponse& response) {
    j = nlohmann::json{{"result", nexigen::exec::to_string(response.result)}};
}

void from_json(const nlohmann::json& j,
               InitialRootStateTransitionResultResponse& response) noexcept {
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
