// nexigen::phm — supervision engine implementation (change add-phm).
#include <nexigen/phm/supervisor.hpp>

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

void Supervisor::record_failure(std::size_t index) noexcept {
    ProcessSupervision& state = states_[index];
    const std::uint64_t tolerance = manifest_.processes[index].failed_supervision_cycles_tolerance;
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
    const SupervisedProcess& process = manifest_.processes[index];

    // Babbling-idiot protection: checkpoints faster than the alive min margin
    // are treated as spam and ignored entirely (REQ_PHM_010).
    if (state.last_checkpoint_time.has_value() &&
        (now - *state.last_checkpoint_time) <
            std::chrono::milliseconds(process.alive_min_margin_ms)) {
        return;
    }

    // Logical supervision: the transition graph must allow last -> current.
    if (state.last_checkpoint.has_value() &&
        !is_allowed_transition(index, *state.last_checkpoint, checkpoint_id)) {
        record_failure(index);
    }

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
                record_failure(index);
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
    if (state.checkpoints_this_window == 0U) {
        record_failure(index);
    } else {
        reset_failures(index);
    }
    state.checkpoints_this_window = 0U;
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
    out += "])";
    return out;
}

} // namespace nexigen::phm
