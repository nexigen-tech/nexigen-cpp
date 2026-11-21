// nexigen::exec — process lifecycle notifications
// (change em-phm-lifecycle-events).
#include <nexigen/exec/process_lifecycle.hpp>

#include <cstdio>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::exec {

std::string ProcessLifecycleEvent::to_string() const {
    return std::format("ProcessLifecycleEvent(app={}, function_group={}, state={})", app,
                       function_group, nexigen::exec::to_string(state));
}

void to_json(nlohmann::json& j, const ProcessLifecycleEvent& event) {
    j = nlohmann::json{{"app", event.app},
                       {"function_group", event.function_group},
                       {"state", nexigen::exec::to_string(event.state)}};
}

void from_json(const nlohmann::json& j, ProcessLifecycleEvent& event) noexcept {
    std::string app;
    if (!core::detail::read_string(j, "app", app)) {
        return;
    }
    std::string function_group;
    if (!core::detail::read_string(j, "function_group", function_group)) {
        return;
    }
    std::string state;
    if (!core::detail::read_string(j, "state", state)) {
        return;
    }
    ProcessLifecycleState parsed = event.state;
    if (!from_string(state, parsed)) {
        return;
    }
    std::snprintf(event.app, sizeof(event.app), "%s", app.c_str());
    std::snprintf(event.function_group, sizeof(event.function_group), "%s", function_group.c_str());
    event.state = parsed;
}

} // namespace nexigen::exec
