// nexigen::phm — application-side PHM client (change add-app-phm-client;
// spec phm/supervised-entity; design D1-D4).
//
// One handle an application uses to participate in health supervision:
//   * report_checkpoint(id)  -> alive / logical / deadline progress (event);
//   * report_health(state)   -> unrecoverable fault reporting (event);
//   * current_group_state(g) -> non-blocking read of the SM-published Function
//     Group state (Field), so the application reacts to SafeState in its tick.
//
// Created on the application's com::Node and called from the
// Infrastructure::run(tick) loop; no Infrastructure policy changes.
#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/com/error.hpp>
#include <nexigen/com/field.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/phm/checkpoint.hpp>
#include <nexigen/phm/health_channel.hpp>
#include <nexigen/phm/health_state.hpp>
#include <nexigen/sm/group_state_payload.hpp>

namespace nexigen::phm {

/// @brief The application's PHM participation handle.
///
/// Bundles the two client-side channels a supervised application needs:
/// `report_checkpoint(id)` drives Alive, Logical, and Deadline progress over
/// the checkpoint event channel, while `report_health(state)` publishes an
/// unrecoverable fault over the health channel. `current_group_state(group)`
/// additionally reads the State Manager's published Function Group state
/// through the Field pattern, so the application can react to a `SafeState`
/// transition in its own tick.
///
/// The handle owns a `com::Node*` plus both channel handles; it is movable but
/// not copyable, and the node must outlive it. All report calls are hot-path
/// (non-blocking transport writes); `to_string` is a cold-path diagnostic.
///
/// @note Spec `phm/supervised-entity`, requirements "Supervised entity
///       configuration", "Checkpoint reporting", "Health status reporting",
///       and "Function Group state read".
class SupervisedEntity {
public:
    /// @brief Open the checkpoint and health channels for the application.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the returned entity.
    /// @param app_name Application identity; copied and used in diagnostics.
    /// @param checkpoint_service Service name of this application's checkpoint
    ///        channel, shared with the daemon's listener.
    /// @param health_service Service name of this application's health channel.
    /// @return The constructed entity only when both channels open, otherwise
    ///         the first mapped `com::Error` from `CheckpointEmitter::create`
    ///         or `HealthChannel::create`.
    /// @note Spec `phm/supervised-entity`, requirement "Supervised entity
    ///       configuration": creation succeeds only when the underlying
    ///       checkpoint and health channels open.
    [[nodiscard]] static com::Result<SupervisedEntity>
    create(com::Node& node, std::string_view app_name, const com::ServiceName& checkpoint_service,
           const com::ServiceName& health_service) noexcept {
        auto checkpoint = CheckpointEmitter::create(node, checkpoint_service);
        if (!checkpoint) {
            return std::unexpected(checkpoint.error());
        }
        auto health = HealthChannel::create(node, health_service);
        if (!health) {
            return std::unexpected(health.error());
        }
        return SupervisedEntity{node, app_name, std::move(*checkpoint), std::move(*health)};
    }

    /// @brief Copy construction is deleted: the entity owns both channels.
    SupervisedEntity(const SupervisedEntity&) = delete;

    /// @brief Copy assignment is deleted: the entity owns both channels.
    SupervisedEntity& operator=(const SupervisedEntity&) = delete;

    /// @brief Move-construct an entity, transferring both channel handles.
    ///
    /// The moved-from entity must not report; its cached listener state moves
    /// with it.
    SupervisedEntity(SupervisedEntity&&) noexcept = default;

    /// @brief Move-assign an entity, transferring both channel handles.
    ///
    /// @return `*this`.
    SupervisedEntity& operator=(SupervisedEntity&&) noexcept = default;

    /// @brief Report one checkpoint (Alive/Logical/Deadline progress).
    ///
    /// Call from each critical cyclic thread. The sequence START ->
    /// intermediate -> END carries the logical and deadline progress the
    /// daemon supervises. The cached last checkpoint advances only when the
    /// emission succeeds.
    ///
    /// @param checkpoint_id Checkpoint identifier from the manifest.
    /// @return Empty `com::Status` when the activation was delivered, or the
    ///         mapped `com::Error` from the checkpoint channel.
    /// @note Spec `phm/supervised-entity`, requirement "Checkpoint reporting".
    [[nodiscard]] com::Status report_checkpoint(std::uint32_t checkpoint_id) noexcept {
        const com::Status status = checkpoint_.emit(checkpoint_id);
        if (status.has_value()) {
            last_checkpoint_ = checkpoint_id;
        }
        return status;
    }

    /// @brief Report a health state; report an unrecoverable fault immediately.
    ///
    /// @param state New health state for this application.
    /// @return Empty `com::Status` when the transition was delivered, or the
    ///         mapped `com::Error` from the health channel.
    /// @note Spec `phm/supervised-entity`, requirement "Health status
    ///       reporting".
    [[nodiscard]] com::Status report_health(HealthState state) noexcept {
        const com::Status status = health_.set_state(state);
        if (status.has_value()) {
            last_health_ = state;
        }
        return status;
    }

    /// @brief Non-blocking read of the State Manager's Function Group state.
    ///
    /// Opens a `com::FieldListener` on `Nexigen/SM/State/<group>` and returns
    /// the last value published through the Field pattern. The listener is
    /// cached for the most recently requested group, so repeated calls for that
    /// group neither reopen the service nor block; asking for a different group
    /// reopens it. The application calls this in its run tick and starts its
    /// safe-mode sequence when the state becomes `SafeState`.
    ///
    /// @param group Function Group name; the service name is derived from it.
    /// @return A success value containing the current payload, `std::nullopt`
    ///         when the listener cannot be opened yet (typically because the SM
    ///         has not published the group), or the mapped `com::Error` when
    ///         the derived service name is invalid.
    /// @note Spec `phm/supervised-entity`, requirement "Function Group state
    ///       read": the read never blocks and never polls the state machine.
    [[nodiscard]] com::Result<std::optional<sm::GroupStatePayload>>
    current_group_state(std::string_view group) noexcept {
        if (!group_listener_.has_value() || group_name_ != group) {
            const std::string service = "Nexigen/SM/State/" + std::string(group);
            const auto name = com::ServiceName::create(service.c_str());
            if (!name) {
                return std::unexpected(name.error());
            }
            auto listener = com::FieldListener<sm::GroupStatePayload>::open(*node_, *name);
            if (!listener) {
                // The SM has not published this group yet: no state, no error.
                return std::nullopt;
            }
            group_listener_.emplace(std::move(*listener));
            group_name_ = std::string(group);
        }
        return group_listener_->get();
    }

    /// @brief The application name this entity reports for.
    ///
    /// @return Borrowed view of the stored name; valid for the entity's
    ///         lifetime.
    /// @note Spec `phm/supervised-entity`, requirement "Supervised entity
    ///       configuration".
    [[nodiscard]] std::string_view app_name() const noexcept { return app_name_; }

    /// @brief Cold-path diagnostic naming the application and last reports.
    ///
    /// @return Text of the form
    ///         `SupervisedEntity(app=<name>, last_checkpoint=<id>,
    ///         last_health=<state>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `phm/supervised-entity`, requirement "Supervised entity
    ///       diagnostics".
    [[nodiscard]] std::string to_string() const {
        return std::format("SupervisedEntity(app={}, last_checkpoint={}, last_health={})",
                           app_name_, last_checkpoint_, nexigen::phm::to_string(last_health_));
    }

private:
    /// @brief Adopt the opened channels and application identity.
    ///
    /// @param node Node used later to open Function Group field listeners.
    /// @param app_name Application identity copied into the entity.
    /// @param checkpoint Opened checkpoint channel.
    /// @param health Opened health channel.
    /// @note Private by design: the only supported construction path is
    ///       `create()`.
    SupervisedEntity(com::Node& node, std::string_view app_name, CheckpointEmitter checkpoint,
                     HealthChannel health) noexcept
        : node_(&node), app_name_(app_name), checkpoint_(std::move(checkpoint)),
          health_(std::move(health)) {}

    com::Node* node_{nullptr};     ///< Borrowed node used for group listeners.
    std::string app_name_;         ///< Application identity for diagnostics.
    CheckpointEmitter checkpoint_; ///< Checkpoint event channel.
    HealthChannel health_;         ///< Health event channel.
    /// Cached group listener; empty until the first successful read.
    std::optional<com::FieldListener<sm::GroupStatePayload>> group_listener_{};
    std::string group_name_{};                 ///< Group name the cached listener belongs to.
    std::uint32_t last_checkpoint_{0};         ///< Last checkpoint accepted by the channel.
    HealthState last_health_{HealthState::Ok}; ///< Last health state accepted.
};

/// @brief Free helper mirroring `SupervisedEntity::to_string()` (mandate 9).
///
/// @param entity Entity to render.
/// @return The entity's diagnostic text.
/// @note Mandate 9; allocates, so cold path only. Spec
///       `phm/supervised-entity`, requirement "Supervised entity diagnostics".
[[nodiscard]] inline std::string to_string(const SupervisedEntity& entity) {
    return entity.to_string();
}

} // namespace nexigen::phm

/// @brief Formats a `SupervisedEntity` as its diagnostic string.
///
/// Delegates to the member `to_string`, so `std::format("{}", entity)` equals
/// the member output (naming the application and last checkpoint/state).
///
/// @note Mandate 11 (universal `std::formatter`); cold path only. Spec
///       `phm/supervised-entity`, requirement "Supervised entity diagnostics".
template <>
struct std::formatter<nexigen::phm::SupervisedEntity> : std::formatter<std::string_view> {
    /// @brief Write the entity's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param entity Entity to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::phm::SupervisedEntity& entity, Ctx& ctx) const {
        const std::string text = entity.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
