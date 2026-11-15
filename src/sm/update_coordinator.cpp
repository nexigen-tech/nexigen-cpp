// nexigen::sm — UCM update coordinator
// (change p3-ucm-coordination).
#include <nexigen/sm/update_coordinator.hpp>

#include <algorithm>
#include <cstdio>
#include <format>
#include <thread>
#include <utility>

#include <nexigen/exec/root_function_group.hpp>

namespace nexigen::sm {

namespace {

constexpr auto retry_interval = std::chrono::milliseconds(10);

[[nodiscard]] std::chrono::milliseconds
remaining_budget(std::chrono::steady_clock::time_point deadline) noexcept {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return std::chrono::milliseconds(1);
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    return remaining.count() == 0 ? std::chrono::milliseconds(1) : remaining;
}

void copy_name(char (&target)[k_ucm_name_length], std::string_view value) noexcept {
    std::snprintf(target, sizeof(target), "%.*s", static_cast<int>(value.size()), value.data());
}

} // namespace

UpdateCoordinator::UpdateCoordinator(StateManager& manager,
                                     com::Client<exec::StateCommand, exec::StateAck>& em_client,
                                     std::string requester_id) noexcept
    : manager_(&manager), em_client_(&em_client), requester_id_(std::move(requester_id)) {}

com::Result<exec::StateResult>
UpdateCoordinator::request_em(std::string_view group, std::string_view target,
                              std::chrono::milliseconds timeout) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    exec::StateCommand command{};
    copy_name(command.function_group, group);
    copy_name(command.target_state, target);
    copy_name(command.requester_id, requester_id_);
    command.request_id = next_request_id_++;
    for (;;) {
        const auto ack = em_client_->call(command, remaining_budget(deadline));
        if (!ack) {
            return std::unexpected(ack.error());
        }
        if (ack->result == exec::StateResult::Success) {
            return ack->result;
        }
        if (ack->result != exec::StateResult::InProgress) {
            return ack->result;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::unexpected(com::Error::Timeout);
        }
        std::this_thread::sleep_for(retry_interval);
    }
}

UpdateResponse UpdateCoordinator::handle(const UpdateRequest& request) noexcept {
    UpdateResponse response{};
    copy_name(response.function_group, request.function_group);
    copy_name(response.active_update_id, active_ ? update_id_ : "");
    if (request.action == UpdateAction::Start) {
        handle_start(request, response);
    } else {
        handle_finish(request, response);
    }
    return response;
}

void UpdateCoordinator::handle_start(const UpdateRequest& request,
                                     UpdateResponse& response) noexcept {
    if (active_) {
        response.result = UpdateResult::Busy;
        return;
    }
    const std::size_t index = manager_->find_group_index(request.function_group);
    if (index == static_cast<std::size_t>(-1)) {
        response.result = UpdateResult::InvalidGroup;
        return;
    }
    if (exec::is_root_function_group(request.function_group)) {
        response.result = UpdateResult::Rejected;
        return;
    }
    if (!manager_->has_state(index, "Update")) {
        response.result = UpdateResult::InvalidState;
        return;
    }
    if (!manager_->application_groups_may_start()) {
        response.result = UpdateResult::Rejected;
        return;
    }
    previous_state_ = std::string(manager_->state(index));
    const auto timeout = std::chrono::milliseconds(manager_->transition_max_time_ms(index));
    const auto em = request_em(request.function_group, "Update", timeout);
    if (!em || *em != exec::StateResult::Success) {
        response.result = UpdateResult::EmFailure;
        return;
    }
    manager_->force_state(index, "Update");
    active_ = true;
    group_index_ = index;
    update_id_ = request.update_id;
    copy_name(response.active_update_id, update_id_);
    response.result = UpdateResult::Accepted;
}

void UpdateCoordinator::handle_finish(const UpdateRequest& request,
                                      UpdateResponse& response) noexcept {
    if (!active_) {
        response.result = UpdateResult::InvalidState;
        return;
    }
    if (std::string_view(request.update_id) != update_id_) {
        response.result = UpdateResult::Rejected;
        return;
    }
    const std::string_view target =
        request.action == UpdateAction::Cancel || request.target_state[0] == '\0'
            ? std::string_view(previous_state_)
            : std::string_view(request.target_state);
    if (!manager_->has_state(group_index_, target)) {
        response.result = UpdateResult::InvalidState;
        return;
    }
    const auto timeout = std::chrono::milliseconds(manager_->transition_max_time_ms(group_index_));
    const auto em = request_em(manager_->group_name(group_index_), target, timeout);
    if (!em || *em != exec::StateResult::Success) {
        response.result = UpdateResult::EmFailure;
        return;
    }
    manager_->force_state(group_index_, target);
    active_ = false;
    update_id_.clear();
    previous_state_.clear();
    response.result = UpdateResult::Accepted;
    response.active_update_id[0] = '\0';
}

com::Result<bool>
UpdateCoordinator::serve_once(com::Server<UpdateRequest, UpdateResponse>& server) noexcept {
    auto received = server.receive();
    if (!received) {
        return std::unexpected(received.error());
    }
    if (!received->has_value()) {
        return false;
    }
    const UpdateResponse response = handle((*received)->payload());
    auto sent = (*received)->send_copy(response);
    if (!sent) {
        return std::unexpected(sent.error());
    }
    return true;
}

std::string UpdateCoordinator::to_string() const {
    return std::format("UpdateCoordinator(active={}, group={}, update_id={})", active_,
                       active_ ? std::string(manager_->group_name(group_index_)) : std::string("-"),
                       update_id_);
}

TransitionResult request_with_update_lock(StateManager& manager, const UpdateCoordinator* update,
                                          std::size_t index, std::string_view target,
                                          std::string_view requester_id) noexcept {
    if (update != nullptr && update->locked(index)) {
        return TransitionResult::RejectedByUpdate;
    }
    return manager.request_transition(index, target, requester_id);
}

} // namespace nexigen::sm
