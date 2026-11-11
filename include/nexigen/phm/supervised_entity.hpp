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

/// The application's PHM participation handle.
class SupervisedEntity {
public:
    /// Open the checkpoint and health channels for the application.
    [[nodiscard]] static com::Result<SupervisedEntity> create(
        com::Node& node, std::string_view app_name, const com::ServiceName& checkpoint_service,
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

    SupervisedEntity(const SupervisedEntity&) = delete;
    SupervisedEntity& operator=(const SupervisedEntity&) = delete;
    SupervisedEntity(SupervisedEntity&&) noexcept = default;
    SupervisedEntity& operator=(SupervisedEntity&&) noexcept = default;

    /// Report one checkpoint (alive/logical/deadline progress); call from the
    /// application's critical cyclic thread.
    [[nodiscard]] com::Status report_checkpoint(std::uint32_t checkpoint_id) noexcept {
        const com::Status status = checkpoint_.emit(checkpoint_id);
        if (status.has_value()) {
            last_checkpoint_ = checkpoint_id;
        }
        return status;
    }

    /// Report a health state; an application that cannot recover a fault
    /// reports it immediately.
    [[nodiscard]] com::Status report_health(HealthState state) noexcept {
        const com::Status status = health_.set_state(state);
        if (status.has_value()) {
            last_health_ = state;
        }
        return status;
    }

    /// Non-blocking read of the Function Group state published by the SM
    /// (`Nexigen/SM/State/<group>`). Returns no value while the group is not
    /// published, and never blocks.
    [[nodiscard]] com::Result<std::optional<sm::GroupStatePayload>> current_group_state(
        std::string_view group) noexcept {
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

    /// The application name this entity reports for.
    [[nodiscard]] std::string_view app_name() const noexcept { return app_name_; }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("SupervisedEntity(app={}, last_checkpoint={}, last_health={})",
                           app_name_, last_checkpoint_, nexigen::phm::to_string(last_health_));
    }

private:
    SupervisedEntity(com::Node& node, std::string_view app_name, CheckpointEmitter checkpoint,
                     HealthChannel health) noexcept
        : node_(&node),
          app_name_(app_name),
          checkpoint_(std::move(checkpoint)),
          health_(std::move(health)) {}

    com::Node* node_{nullptr};
    std::string app_name_;
    CheckpointEmitter checkpoint_;
    HealthChannel health_;
    std::optional<com::FieldListener<sm::GroupStatePayload>> group_listener_{};
    std::string group_name_{};
    std::uint32_t last_checkpoint_{0};
    HealthState last_health_{HealthState::Ok};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const SupervisedEntity& entity) {
    return entity.to_string();
}

} // namespace nexigen::phm

/// The entity formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::phm::SupervisedEntity> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::phm::SupervisedEntity& entity, Ctx& ctx) const {
        const std::string text = entity.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
