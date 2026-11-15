// nexigen::sm — UCM update coordination vocabulary
// (change p3-ucm-coordination).
#include <nexigen/sm/ucm.hpp>

#include <cstddef>
#include <cstdio>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::sm {

std::string UpdateRequest::to_string() const {
    return std::format("UpdateRequest(function_group={}, update_id={}, action={}, target_state={})",
                       function_group, update_id, nexigen::sm::to_string(action), target_state);
}

std::string UpdateResponse::to_string() const {
    return std::format("UpdateResponse(result={}, function_group={}, active_update_id={})",
                       nexigen::sm::to_string(result), function_group, active_update_id);
}

namespace {

void read_name(const nlohmann::json& j, const char* key, char* out, std::size_t size) noexcept {
    std::string value;
    if (core::detail::read_string(j, key, value)) {
        std::snprintf(out, size, "%s", value.c_str());
    }
}

} // namespace

void to_json(nlohmann::json& j, const UpdateRequest& value) {
    j = nlohmann::json{{"function_group", value.function_group},
                       {"update_id", value.update_id},
                       {"action", nexigen::sm::to_string(value.action)},
                       {"target_state", value.target_state}};
}

void from_json(const nlohmann::json& j, UpdateRequest& value) noexcept {
    read_name(j, "function_group", value.function_group, sizeof(value.function_group));
    read_name(j, "update_id", value.update_id, sizeof(value.update_id));
    std::string action;
    if (core::detail::read_string(j, "action", action)) {
        UpdateAction parsed = value.action;
        if (from_string(action, parsed)) {
            value.action = parsed;
        }
    }
    read_name(j, "target_state", value.target_state, sizeof(value.target_state));
}

void to_json(nlohmann::json& j, const UpdateResponse& value) {
    j = nlohmann::json{{"result", nexigen::sm::to_string(value.result)},
                       {"function_group", value.function_group},
                       {"active_update_id", value.active_update_id}};
}

void from_json(const nlohmann::json& j, UpdateResponse& value) noexcept {
    std::string result;
    if (core::detail::read_string(j, "result", result)) {
        UpdateResult parsed = value.result;
        if (from_string(result, parsed)) {
            value.result = parsed;
        }
    }
    read_name(j, "function_group", value.function_group, sizeof(value.function_group));
    read_name(j, "active_update_id", value.active_update_id, sizeof(value.active_update_id));
}

} // namespace nexigen::sm
