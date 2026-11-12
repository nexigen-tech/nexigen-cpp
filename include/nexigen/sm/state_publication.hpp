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

/// @brief Publishes every configured Function Group's current state as a Field.
///
/// One Field per group is created on the node under
/// `Nexigen/SM/State/<group>`, seeded with the group's first declared state.
/// Because the Field keeps only the latest sample, a late reader observes the
/// current state immediately and no request-response round trip is needed.
/// The publisher is move-only and confined to the arbitration thread; it holds
/// the Field handles for its whole lifetime.
///
/// @note Spec `sm/ipc`, requirement "Current state publication".
class StatePublication {
public:
    /// @brief Create one Field per Function Group on the node.
    ///
    /// @param node Node on which the Fields are created.
    /// @param manifest Manifest whose groups and initial states are published.
    /// @return The publication by value on success, or the first
    ///         `ServiceName::create`/`Field::create` error; never calls
    ///         `value()` on a failed result.
    /// @note Spec `sm/ipc`, requirement "Current state publication"
    ///       (`history_size = 1`).
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

    /// @brief Move the Field handles to a new owner.
    StatePublication(StatePublication&&) noexcept = default;

    /// @brief Move-assign the Field handles.
    StatePublication& operator=(StatePublication&&) noexcept = default;

    /// @brief Publish one group's current state.
    ///
    /// Overwrites the group's Field sample, so a late reader observes the new
    /// value immediately. The name is copied as a C string and truncated to
    /// `k_group_state_length - 1` bytes.
    ///
    /// @param index Group index in manifest order.
    /// @param state New NUL-terminated state name.
    /// @return Empty `com::Status` on success, or the Field publish error.
    /// @warning `index` must be below `manifest.function_groups.size()`; the
    ///          accessor performs no bounds check. `state` must be
    ///          NUL-terminated because it is formatted as a C string.
    /// @note Spec `sm/ipc`, requirement "Current state publication".
    [[nodiscard]] com::Status publish(std::size_t index, std::string_view state) noexcept {
        GroupStatePayload payload{};
        std::snprintf(payload.state, sizeof(payload.state), "%s", state.data());
        return fields_[index].set(std::move(payload));
    }

    /// @brief Render the publication as a diagnostic string.
    ///
    /// @return Text naming the number of published groups; allocates.
    /// @note Spec `sm/ipc`, requirement "Current state publication".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("StatePublication(groups={})", fields_.size());
    }

private:
    /// @brief Construct an empty publication; `create()` fills the Fields.
    StatePublication() = default;

    std::vector<com::Field<GroupStatePayload>> fields_; ///< One Field per group.
};

/// @brief Free diagnostic helper mirroring `StatePublication::to_string`.
///
/// @param publication Publication to render.
/// @return The member `to_string()` output.
/// @note Spec `sm/ipc`, requirement "Current state publication".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const StatePublication& publication) {
    return publication.to_string();
}

} // namespace nexigen::sm

/// @brief `std::formatter` for `StatePublication` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the member
/// diagnostic, so `std::format("{}", publication)` equals `to_string()`.
///
/// @note Spec `sm/ipc`, requirement "Current state publication".
template <>
struct std::formatter<nexigen::sm::StatePublication> : std::formatter<std::string_view> {
    /// @brief Format the publication as its diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param publication Publication to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx>
    auto format(const nexigen::sm::StatePublication& publication, Ctx& ctx) const {
        const std::string text = publication.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
