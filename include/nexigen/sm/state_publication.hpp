// nexigen::sm — current-state publication (change add-sm; spec sm/ipc
// "Current state publication"; design D2).
//
// Publishes every Function Group's current state through the com Field
// pattern (history_size = 1): any process starting later reads the current
// status from shared memory immediately without sending a request. The state
// travels as a fixed char buffer (Field payloads must be trivially copyable).
#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/field.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/sm/group_state_payload.hpp>
#include <nexigen/sm/sm_manifest.hpp>

namespace nexigen::sm {

/// Publishes the current state of every configured Function Group as a Field.
class StatePublication {
public:
    /// Create one Field per Function Group on the node.
    [[nodiscard]] static com::Result<StatePublication> create(com::Node& node,
                                                              const SmManifest& manifest) noexcept {
        StatePublication publication;
        for (const FunctionGroup& group : manifest.function_groups) {
            const std::string service_name = "Nexigen/SM/State/" + group.name;
            const auto name = com::ServiceName::create(service_name.c_str());
            if (!name) {
                return std::unexpected(name.error());
            }
            GroupStatePayload initial{};
            if (!group.states.empty()) {
                std::snprintf(initial.state, sizeof(initial.state), "%s",
                              group.states.front().c_str());
            }
            auto field = com::Field<GroupStatePayload>::create(node, *name, initial);
            if (!field) {
                return std::unexpected(field.error());
            }
            publication.fields_.push_back(std::move(*field));
        }
        return publication;
    }

    StatePublication(StatePublication&&) noexcept = default;
    StatePublication& operator=(StatePublication&&) noexcept = default;

    /// Publish one group's current state; late readers observe it immediately.
    [[nodiscard]] com::Status publish(std::size_t index, std::string_view state) noexcept {
        GroupStatePayload payload{};
        std::snprintf(payload.state, sizeof(payload.state), "%s", state.data());
        return fields_[index].set(std::move(payload));
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("StatePublication(groups={})", fields_.size());
    }

private:
    StatePublication() = default;

    std::vector<com::Field<GroupStatePayload>> fields_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const StatePublication& publication) {
    return publication.to_string();
}

} // namespace nexigen::sm

/// The publication formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::sm::StatePublication> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::sm::StatePublication& publication, Ctx& ctx) const {
        const std::string text = publication.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
