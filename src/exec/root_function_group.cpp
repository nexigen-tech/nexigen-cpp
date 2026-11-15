// nexigen::exec — reserved RootFG validation
// (change machinefg-as-em-function-group).
#include <nexigen/exec/root_function_group.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec {

core::Status validate_root_function_group(const FunctionGroupConfig& group) noexcept {
    if (!is_root_function_group(group.name)) {
        return {};
    }
    if (group.states.size() != root_function_group_states().size()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (const std::string_view required : root_function_group_states()) {
        if (std::find(group.states.begin(), group.states.end(), required) == group.states.end()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
    }
    if (!group.initial_state.empty() && group.initial_state != k_root_state_off &&
        group.initial_state != k_root_state_startup) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

} // namespace nexigen::exec
