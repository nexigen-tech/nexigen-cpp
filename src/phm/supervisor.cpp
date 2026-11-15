// nexigen::phm — supervision engine implementation (change add-phm).
#include <nexigen/phm/supervisor.hpp>

#include <cstdio>
#include <utility>

namespace nexigen::phm {

Supervisor::Supervisor(PhmManifest manifest) : manifest_(std::move(manifest)) {
    states_.reserve(manifest_.processes.size());
    for (const SupervisedProcess& process : manifest_.processes) {
        ProcessSupervision state;
        state.deadline_clocks.resize(process.deadlines.size());
        states_.push_back(std::move(state));
    }
}

std::size_t Supervisor::process_count() const noexcept {
    return states_.size();
}

const SupervisedProcess& Supervisor::process(std::size_t index) const noexcept {
    return manifest_.processes[index];
}

SupervisionStatus Supervisor::status(std::size_t index) const noexcept {
    return states_[index].status;
}

bool Supervisor::is_allowed_transition(std::size_t index, std::uint32_t from,
                                       std::uint32_t to) const noexcept {
    for (const CheckpointTransition& transition : manifest_.processes[index].transitions) {
        if (transition.from_id == from && transition.to_id == to) {
            return true;
        }
    }
    return false;
}

void Supervisor::record_failure(std::size_t index, FaultKind kind) noexcept {
    ProcessSupervision& state = states_[index];
    if (state.lifecycle == EmLifecycleState::Terminating ||
        state.lifecycle == EmLifecycleState::Terminated ||
        state.lifecycle == EmLifecycleState::Failed) {
        return; // Deliberate EM stop or spent budget: never a supervision fault.
    }
    const std::uint64_t tolerance = manifest_.processes[index].failed_supervision_cycles_tolerance;
    state.last_fault = kind;
    state.consecutive_failures += 1U;
    if (state.consecutive_failures > tolerance) {
        state.status = SupervisionStatus::Expired;
    } else {
        state.status = SupervisionStatus::Failed;
    }
}

void Supervisor::reset_failures(std::size_t index) noexcept {
    ProcessSupervision& state = states_[index];
    state.consecutive_failures = 0;
    state.status = SupervisionStatus::Ok;
}

void Supervisor::on_checkpoint(std::size_t index, std::uint32_t checkpoint_id,
                               Clock::time_point now) noexcept {
    ProcessSupervision& state = states_[index];
    if (state.lifecycle == EmLifecycleState::Terminating ||
        state.lifecycle == EmLifecycleState::Terminated ||
        state.lifecycle == EmLifecycleState::Failed) {
        return;
    }
    const SupervisedProcess& process = manifest_.processes[index];

    // Babbling-idiot protection: checkpoints faster than the alive min margin
    // are treated as spam and ignored entirely (REQ_PHM_010).
    if (state.last_checkpoint_time.has_value() &&
        (now - *state.last_checkpoint_time) <
            std::chrono::milliseconds(process.alive_min_margin_ms)) {
        return;
    }

    // Logical supervision: the transition graph must allow last -> current,
    // and the declared fork/join regions must be respected.
    if (state.last_checkpoint.has_value() &&
        !is_allowed_transition(index, *state.last_checkpoint, checkpoint_id)) {
        record_failure(index, FaultKind::LogicalFailure);
    }
    apply_fork_join(index, checkpoint_id);

    // Deadline supervision: source starts a clock, target checks min/max.
    for (std::size_t i = 0; i < process.deadlines.size(); ++i) {
        const DeadlinePair& pair = process.deadlines[i];
        DeadlineClock& clock = state.deadline_clocks[i];
        if (checkpoint_id == pair.source_id) {
            clock.started = now;
        } else if (checkpoint_id == pair.target_id && clock.started.has_value()) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - *clock.started);
            if (elapsed.count() < 0 || static_cast<std::uint64_t>(elapsed.count()) < pair.min_ms ||
                static_cast<std::uint64_t>(elapsed.count()) > pair.max_ms) {
                record_failure(index, FaultKind::DeadlineFailure);
            }
            clock.started.reset();
        }
    }

    state.last_checkpoint = checkpoint_id;
    state.last_checkpoint_time = now;
    state.checkpoints_this_window += 1U;
}

void Supervisor::on_alive_tick(std::size_t index, Clock::time_point /*now*/) noexcept {
    ProcessSupervision& state = states_[index];
    if (state.lifecycle == EmLifecycleState::Terminating ||
        state.lifecycle == EmLifecycleState::Terminated ||
        state.lifecycle == EmLifecycleState::Failed) {
        state.checkpoints_this_window = 0U;
        return;
    }
    const SupervisedProcess& process = manifest_.processes[index];
    const std::int64_t expected = process.alive_expected_indications;
    if (expected <= 0) {
        // Legacy rule: at least one checkpoint per window.
        if (state.checkpoints_this_window == 0U) {
            record_failure(index, FaultKind::AliveFailure);
        } else {
            reset_failures(index);
        }
    } else {
        const std::uint64_t wanted = static_cast<std::uint64_t>(expected);
        const std::uint64_t shortfall = static_cast<std::uint64_t>(process.alive_min_margin_cycles);
        const std::uint64_t excess = static_cast<std::uint64_t>(process.alive_max_margin_cycles);
        const std::uint64_t lower = wanted > shortfall ? wanted - shortfall : 0U;
        const std::uint64_t upper = wanted + excess;
        const std::uint64_t count = state.checkpoints_this_window;
        if (count < lower || count > upper) {
            record_failure(index, FaultKind::AliveFailure);
        } else {
            reset_failures(index);
        }
    }
    state.checkpoints_this_window = 0U;
}

void Supervisor::on_execution_state(std::size_t index, EmLifecycleState state,
                                    std::string_view function_group) noexcept {
    ProcessSupervision& supervision = states_[index];
    supervision.lifecycle = state;
    ++lifecycle_notifications_;
    if (!function_group.empty()) {
        std::snprintf(supervision.function_group, sizeof(supervision.function_group), "%.*s",
                      static_cast<int>(function_group.size()), function_group.data());
    }
    if (state == EmLifecycleState::Failed) {
        supervision.consecutive_failures = 0U;
        supervision.status = SupervisionStatus::Exhausted;
        supervision.checkpoints_this_window = 0U;
        supervision.last_checkpoint.reset();
        supervision.last_checkpoint_time.reset();
        supervision.fork_depth = 0U;
        for (ForkFrame& frame : supervision.fork_stack) {
            frame.entered.reset();
        }
        for (DeadlineClock& clock : supervision.deadline_clocks) {
            clock.started.reset();
        }
        return;
    }
    if (state == EmLifecycleState::Running || state == EmLifecycleState::Terminating ||
        state == EmLifecycleState::Terminated) {
        supervision.consecutive_failures = 0U;
        supervision.status = SupervisionStatus::Ok;
        supervision.checkpoints_this_window = 0U;
        supervision.last_checkpoint.reset();
        supervision.last_checkpoint_time.reset();
        supervision.fork_depth = 0U;
        for (ForkFrame& frame : supervision.fork_stack) {
            frame.entered.reset();
        }
        for (DeadlineClock& clock : supervision.deadline_clocks) {
            clock.started.reset();
        }
    }
}

void Supervisor::apply_fork_join(std::size_t index, std::uint32_t checkpoint_id) noexcept {
    const std::vector<LogicalFork>& forks = manifest_.processes[index].logical_forks;
    if (forks.empty()) {
        return;
    }
    ProcessSupervision& state = states_[index];

    // A join closes its frame, which must be the innermost one and must have
    // every branch entered. Skipping frames or missing branches is a fault.
    for (std::size_t depth = state.fork_depth; depth > 0U; --depth) {
        const ForkFrame& frame = state.fork_stack[depth - 1U];
        const LogicalFork& fork = forks[frame.fork_index];
        if (fork.join_id != checkpoint_id) {
            continue;
        }
        if (depth != state.fork_depth) {
            record_failure(index, FaultKind::LogicalFailure); // Frames above this one were skipped.
        }
        if (frame.entered.count() != fork.branch_entries.size()) {
            record_failure(index, FaultKind::LogicalFailure); // Unentered branches.
        }
        state.fork_depth = depth - 1U;
        return;
    }

    // A declared join with no open frame is out of order.
    for (const LogicalFork& fork : forks) {
        if (fork.join_id == checkpoint_id) {
            record_failure(index, FaultKind::LogicalFailure);
            return;
        }
    }

    // A fork opens a new frame; the manifest bound guarantees capacity.
    for (std::size_t f = 0; f < forks.size(); ++f) {
        if (forks[f].fork_id != checkpoint_id) {
            continue;
        }
        if (state.fork_depth >= state.fork_stack.size()) {
            record_failure(index, FaultKind::LogicalFailure);
            return;
        }
        state.fork_stack[state.fork_depth] = ForkFrame{f, {}};
        ++state.fork_depth;
        return;
    }

    // A branch entry marks its bit in the innermost open frame.
    if (state.fork_depth == 0U) {
        return;
    }
    const ForkFrame& frame = state.fork_stack[state.fork_depth - 1U];
    const LogicalFork& fork = forks[frame.fork_index];
    for (std::size_t b = 0; b < fork.branch_entries.size(); ++b) {
        if (fork.branch_entries[b] == checkpoint_id) {
            state.fork_stack[state.fork_depth - 1U].entered.set(b);
            return;
        }
    }
}

EmLifecycleState Supervisor::em_lifecycle(std::size_t index) const noexcept {
    return states_[index].lifecycle;
}

std::string_view Supervisor::function_group(std::size_t index) const noexcept {
    return std::string_view(states_[index].function_group);
}

FaultKind Supervisor::last_fault(std::size_t index) const noexcept {
    return states_[index].last_fault;
}

bool Supervisor::exhausted(std::size_t index) const noexcept {
    return states_[index].status == SupervisionStatus::Exhausted;
}

std::string Supervisor::to_string() const {
    std::string out = "Supervisor([";
    for (std::size_t i = 0; i < states_.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += std::format("{}={}", manifest_.processes[i].name,
                           nexigen::phm::to_string(states_[i].status));
    }
    out += std::format("], lifecycle={})", lifecycle_notifications_);
    return out;
}

} // namespace nexigen::phm
