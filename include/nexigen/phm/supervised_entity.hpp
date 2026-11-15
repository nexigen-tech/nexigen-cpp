// nexigen::phm — application-side PHM client (change add-app-phm-client;
// spec phm/supervised-entity; design D1-D4).
//
// One handle an application uses to participate in health supervision:
//   * report_checkpoint(id)  -> alive / logical / deadline progress (event);
//   * report_health(state)   -> unrecoverable fault reporting (event);
//   * current_group_state(g) -> non-blocking read of the SM-published Function
//     Group state (Field), so the application reacts to SafeState in its tick;
//   * claim_identity()       -> optional identity claim for a gated manifest
//     entry, also re-published on an interval from report_checkpoint.
//
// Created on the application's com::Node and called from the
// Infrastructure::run(tick) loop; no Infrastructure policy changes.
#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <unistd.h>

#include <nexigen/com/error.hpp>
#include <nexigen/com/field.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/phm/checkpoint.hpp>
#include <nexigen/phm/claim_publisher.hpp>
#include <nexigen/phm/health_channel.hpp>
#include <nexigen/phm/health_state.hpp>
#include <nexigen/sm/group_state_payload.hpp>

namespace nexigen::phm {

/// @brief Optional identity-claim wiring for a supervised entity.
///
/// When present, the entity publishes `{name, pid, uid}` at creation and then
/// again at most once per `interval`, so a daemon that restarts (or starts
/// after the application) learns the identity without a handshake. The claim
/// is also published on demand through `SupervisedEntity::claim_identity`.
///
/// @note Spec `phm/identity`, requirement "Checkpoint claim channel". Spec
///       `phm/supervised-entity`, requirement "Optional identity claim from the
///       supervised entity", scenarios "Creation claims the identity" and "A
///       periodic claim repairs a lost one".
struct ClaimConfig {
    com::ServiceName channel; ///< Claim sample service (for example `Nexigen/PHM/Claim`).
    com::ServiceName event;   ///< Paired wake event (for example `Nexigen/PHM/ClaimEvent`).
    /// Re-claim period; the bounded retry keeps a daemon restart self-healing.
    /// One second is the default: frequent enough to recover before a typical
    /// Alive window expires, cheap enough to stay off the hot path in practice.
    std::chrono::milliseconds interval{1000};
};

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
/// An entity created with a `ClaimConfig` also owns an identity-claim channel:
/// it publishes the claim during creation and re-publishes it at most once per
/// configured interval while checkpoints are reported.
///
/// @note Spec `phm/supervised-entity`, requirements "Supervised entity
///       configuration", "Checkpoint reporting", "Health status reporting",
///       and "Function Group state read". Spec `phm/identity`, requirement
///       "Checkpoint claim channel".
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
    /// @param claim_config Optional identity-claim wiring; when present the
    ///        entity must publish its first claim successfully.
    /// @return The constructed entity only when every requested channel opens,
    ///         otherwise the first mapped `com::Error` from
    ///         `CheckpointEmitter::create`, `HealthChannel::create`, or
    ///         `ClaimPublisher::create`, or the claim send error.
    /// @note Spec `phm/supervised-entity`, requirement "Supervised entity
    ///       configuration": creation succeeds only when the underlying
    ///       checkpoint and health channels open. Spec `phm/identity`,
    ///       requirement "Checkpoint claim channel". Spec
    ///       `phm/supervised-entity`, requirement "Optional identity claim from
    ///       the supervised entity", scenario "Creation claims the identity":
    ///       with a `ClaimConfig`, a failed first claim also fails creation.
    [[nodiscard]] static com::Result<SupervisedEntity>
    create(com::Node& node, std::string_view app_name, const com::ServiceName& checkpoint_service,
           const com::ServiceName& health_service,
           const std::optional<ClaimConfig>& claim_config = std::nullopt) noexcept {
        auto checkpoint = CheckpointEmitter::create(node, checkpoint_service);
        if (!checkpoint) {
            return std::unexpected(checkpoint.error());
        }
        auto health = HealthChannel::create(node, health_service);
        if (!health) {
            return std::unexpected(health.error());
        }
        SupervisedEntity entity{node, app_name, std::move(*checkpoint), std::move(*health)};
        if (claim_config.has_value()) {
            auto publisher =
                ClaimPublisher::create(node, claim_config->channel, claim_config->event);
            if (!publisher) {
                return std::unexpected(publisher.error());
            }
            entity.claim_.emplace(std::move(*publisher));
            entity.claim_pid_ = static_cast<std::uint32_t>(::getpid());
            entity.claim_uid_ = ::getuid();
            entity.claim_interval_ = claim_config->interval;
            if (const com::Status claimed = entity.claim_identity(); !claimed) {
                return std::unexpected(claimed.error());
            }
        }
        return entity;
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
    ///       For an entity with a `ClaimConfig`, the call also refreshes the
    ///       identity claim when the retry interval elapsed (spec
    ///       `phm/identity`, scenario "A lost claim is retried"); the refresh
    ///       never fails the checkpoint report.
    [[nodiscard]] com::Status report_checkpoint(std::uint32_t checkpoint_id) noexcept {
        maybe_reclaim_identity();
        const com::Status status = checkpoint_.emit(checkpoint_id);
        if (status.has_value()) {
            last_checkpoint_ = checkpoint_id;
        }
        return status;
    }

    /// @brief Publish the identity claim now, bypassing the retry interval.
    ///
    /// A no-op when the entity was created without a `ClaimConfig`. The call is
    /// rate-limited afterwards: the next automatic re-claim happens one
    /// interval after this attempt.
    ///
    /// @return Empty `com::Status` when the claim and its wake event were
    ///         delivered (or when no claim channel is configured), otherwise
    ///         the mapped `com::Error` from the claim channel.
    /// @note Spec `phm/identity`, requirement "Checkpoint claim channel". Spec
    ///       `phm/supervised-entity`, requirement "Optional identity claim from
    ///       the supervised entity", scenario "On-demand claim is available".
    [[nodiscard]] com::Status claim_identity() noexcept {
        if (!claim_.has_value()) {
            return {};
        }
        next_claim_ = std::chrono::steady_clock::now() + claim_interval_;
        return claim_->claim(app_name_, claim_pid_, claim_uid_);
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
    /// @brief Publish the claim when the retry interval elapsed.
    ///
    /// Called from the checkpoint hot path: it only reads the steady clock and
    /// compares a time point, so a legacy entity pays nothing. A failed send
    /// is retried after the next interval instead of on every checkpoint.
    ///
    /// @note Spec `phm/identity`, requirement "Checkpoint claim channel",
    ///       scenario "A lost claim is retried".
    void maybe_reclaim_identity() noexcept {
        if (!claim_.has_value()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now < next_claim_) {
            return;
        }
        next_claim_ = now + claim_interval_;
        static_cast<void>(claim_->claim(app_name_, claim_pid_, claim_uid_));
    }

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
    /// Optional identity-claim channel; empty for a legacy entity.
    std::optional<ClaimPublisher> claim_{};
    std::uint32_t claim_pid_{0};                         ///< Claimed pid (creation time).
    std::uint32_t claim_uid_{0};                         ///< Claimed real uid (creation time).
    std::chrono::milliseconds claim_interval_{1000};     ///< Re-claim period.
    std::chrono::steady_clock::time_point next_claim_{}; ///< Earliest next automatic claim.
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
