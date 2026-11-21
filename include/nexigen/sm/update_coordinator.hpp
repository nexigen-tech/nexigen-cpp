// nexigen::sm — UCM update coordinator
// (change p3-ucm-coordination; spec sm/ucm-coordination).
//
// Owns the single-update lock, the previous-state capture, and the EM command
// sequence used by Start/Complete/Cancel. Served from the SM arbitration loop;
// cold path only.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/com/client.hpp>
#include <nexigen/com/server.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_command.hpp>
#include <nexigen/sm/state_manager.hpp>
#include <nexigen/sm/ucm.hpp>

namespace nexigen::sm {

/// @brief Handles UCM Start/Complete/Cancel over one Function Group.
///
/// @note Spec `sm/ucm-coordination`, requirement "Update start".
class UpdateCoordinator {
public:
    /// @brief Bind the coordinator to the arbiter and the EM command channel.
    ///
    /// @param manager SM arbiter; borrowed, must outlive the coordinator.
    /// @param em_client EM `StateCommand` client; borrowed.
    /// @param requester_id Requester identity sent to the EM.
    UpdateCoordinator(StateManager& manager,
                      com::Client<exec::StateCommand, exec::StateAck>& em_client,
                      std::string requester_id) noexcept;

    /// @brief Handle one UCM request.
    ///
    /// @param request Start, Complete or Cancel request to apply.
    /// @return The reply for the requester, with the resulting update state.
    [[nodiscard]] UpdateResponse handle(const UpdateRequest& request) noexcept;

    /// @brief Receive and answer one pending request from `server`.
    ///
    /// @param server UCM request-response server.
    /// @return `true` when a request was handled, `false` when none was pending,
    ///         or the com error from receive/acknowledge.
    [[nodiscard]] com::Result<bool>
    serve_once(com::Server<UpdateRequest, UpdateResponse>& server) noexcept;

    /// @brief Whether one group is locked by an active update.
    ///
    /// @param group_index Group index.
    /// @return `true` when that group is the target of the active update.
    [[nodiscard]] bool locked(std::size_t group_index) const noexcept {
        return active_ && group_index_ == group_index;
    }

    /// @brief Whether an update is active.
    ///
    /// @return `true` while a Start has not been completed or cancelled.
    [[nodiscard]] bool active() const noexcept { return active_; }

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return A human-readable summary of the active update.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Apply one `Start` request.
    void handle_start(const UpdateRequest& request, UpdateResponse& response) noexcept;

    /// @brief Apply one `Complete`/`Cancel` request.
    void handle_finish(const UpdateRequest& request, UpdateResponse& response) noexcept;

    /// @brief Send one EM state request and wait for a terminal result.
    [[nodiscard]] com::Result<exec::StateResult>
    request_em(std::string_view group, std::string_view target,
               std::chrono::milliseconds timeout) noexcept;

    StateManager* manager_{nullptr};
    com::Client<exec::StateCommand, exec::StateAck>* em_client_{nullptr};
    std::string requester_id_{};
    std::uint64_t next_request_id_{1};
    bool active_{false};
    std::size_t group_index_{0};
    std::string update_id_{};
    std::string previous_state_{};
};

/// @brief Apply the UCM lock before a TriggerIn transition.
///
/// @param manager SM arbiter.
/// @param update Optional active coordinator.
/// @param index Group index.
/// @param target Requested state.
/// @param requester_id Requester identity.
/// @return `RejectedByUpdate` when the group is locked, otherwise the normal
///         arbitration result.
[[nodiscard]] TransitionResult request_with_update_lock(StateManager& manager,
                                                        const UpdateCoordinator* update,
                                                        std::size_t index, std::string_view target,
                                                        std::string_view requester_id) noexcept;

} // namespace nexigen::sm

template <>
struct std::formatter<nexigen::sm::UpdateCoordinator> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::UpdateCoordinator& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
