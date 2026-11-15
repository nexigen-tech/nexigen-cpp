// nexigen::sm — LSM runtime binding
// (change p3-lsm-runtime-integration).
#include <nexigen/sm/lsm_runtime.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <thread>

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

void copy_name(char* target, std::size_t size, std::string_view value) noexcept {
    std::snprintf(target, size, "%.*s", static_cast<int>(value.size()), value.data());
}

[[nodiscard]] bool is_off_like(std::string_view state) noexcept {
    return state == "Off" || state == "SafeState";
}

} // namespace

class LsmRuntime::Sink final : public LsmActionSink {
public:
    Sink(StateManager& manager, com::Client<exec::StateCommand, exec::StateAck>& em_client,
         StatePublication& publication) noexcept
        : manager_(&manager), em_client_(&em_client), publication_(&publication) {}

    core::Status request_state(std::string_view group, std::string_view state,
                               std::string_view requester_id) override {
        const std::size_t index = manager_->find_group_index(group);
        if (index == static_cast<std::size_t>(-1)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(manager_->transition_max_time_ms(index));
        exec::StateCommand command{};
        copy_name(command.function_group, sizeof(command.function_group), group);
        copy_name(command.target_state, sizeof(command.target_state), state);
        copy_name(command.requester_id, sizeof(command.requester_id), requester_id);
        command.request_id = next_request_id_++;
        for (;;) {
            const auto ack = em_client_->call(command, remaining_budget(deadline));
            if (!ack) {
                return std::unexpected(core::ErrorCode::SystemError);
            }
            if (ack->result == exec::StateResult::Success) {
                manager_->force_state(index, state);
                return {};
            }
            if (ack->result != exec::StateResult::InProgress ||
                std::chrono::steady_clock::now() >= deadline) {
                return std::unexpected(core::ErrorCode::SystemError);
            }
            std::this_thread::sleep_for(retry_interval);
        }
    }

    core::Status publish_state(std::string_view group, std::string_view state) override {
        const std::size_t index = manager_->find_group_index(group);
        if (index == static_cast<std::size_t>(-1)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        const com::Status published = publication_->publish(index, state);
        return published ? core::Status{} : std::unexpected(core::ErrorCode::SystemError);
    }

private:
    StateManager* manager_{nullptr};
    com::Client<exec::StateCommand, exec::StateAck>* em_client_{nullptr};
    StatePublication* publication_{nullptr};
    std::uint64_t next_request_id_{1};
};

LsmRuntime::LsmRuntime(std::unique_ptr<Sink> sink, StateManager& manager)
    : sink_(std::move(sink)), manager_(&manager) {}

LsmRuntime::LsmRuntime(LsmRuntime&&) noexcept = default;
LsmRuntime& LsmRuntime::operator=(LsmRuntime&&) noexcept = default;
LsmRuntime::~LsmRuntime() = default;

core::Result<LsmRuntime>
LsmRuntime::create(const SmManifest& manifest, StateManager& manager,
                   com::Client<exec::StateCommand, exec::StateAck>& em_client,
                   StatePublication& publication) {
    auto sink = std::make_unique<Sink>(manager, em_client, publication);
    LsmRuntime runtime(std::move(sink), manager);
    for (const LightweightStateMachineConfig& config : manifest.state_machines) {
        const std::size_t index = manager.find_group_index(config.function_group);
        if (index == static_cast<std::size_t>(-1)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        auto machine = LightweightStateMachine::create(config, *runtime.sink_);
        if (!machine) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (!machine->reset_to(manager.state(index))) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        runtime.bindings_.push_back(Binding{index, std::move(*machine)});
    }
    return runtime;
}

bool LsmRuntime::handles(std::size_t group_index) const noexcept {
    return std::any_of(bindings_.begin(), bindings_.end(), [group_index](const Binding& binding) {
        return binding.group_index == group_index;
    });
}

TransitionResult LsmRuntime::request(std::size_t group_index, std::string_view target,
                                     std::string_view requester_id) noexcept {
    if (manager_->is_application_group(group_index) && !manager_->application_groups_may_start() &&
        !is_off_like(target)) {
        return TransitionResult::InvalidGraph;
    }
    for (Binding& binding : bindings_) {
        if (binding.group_index != group_index) {
            continue;
        }
        const auto result = binding.machine.request(target, requester_id);
        manager_->force_state(group_index, binding.machine.current_state());
        if (!result) {
            return TransitionResult::ActionFailed;
        }
        return *result;
    }
    return TransitionResult::InvalidGraph;
}

std::string LsmRuntime::to_string() const {
    std::string out = "LsmRuntime(bindings=[";
    for (std::size_t i = 0; i < bindings_.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += manager_->group_name(bindings_[i].group_index);
        out += "=";
        out += bindings_[i].machine.current_state();
    }
    out += "])";
    return out;
}

} // namespace nexigen::sm
