// nexigen::phm — supervision engine (change add-phm; spec phm/supervision;
// design D3).
//
// Headless per-process state machines fed by checkpoint activations and
// interval ticks. Alive counts checkpoints per window (at least one expected
// per cycle), Logical walks the allowed-transition graph, Deadline times
// source->target pairs against min/max, and a tolerance counter drives
// Ok -> Failed -> Expired. All hot paths are O(1) in the manifest size (small
// linear scans over pre-allocated vectors, no allocations, no locks — the
// daemon thread is the single writer).
#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <nexigen/phm/phm_manifest.hpp>
#include <nexigen/phm/supervision_status.hpp>

namespace nexigen::phm {

/// The supervision engine over a manifest's process list.
class Supervisor {
public:
    using Clock = std::chrono::steady_clock;

    /// Build the engine from an immutable manifest snapshot.
    explicit Supervisor(PhmManifest manifest);

    /// Number of supervised processes.
    [[nodiscard]] std::size_t process_count() const noexcept;

    /// The manifest entry of one supervised process.
    [[nodiscard]] const SupervisedProcess& process(std::size_t index) const noexcept;

    /// Current aggregated supervision status of one process.
    [[nodiscard]] SupervisionStatus status(std::size_t index) const noexcept;

    /// Feed one checkpoint activation (event id) received from a process.
    /// Checkpoints arriving faster than the alive min margin are ignored
    /// (babbling-idiot protection); logical and deadline violations are
    /// recorded immediately.
    void on_checkpoint(std::size_t index, std::uint32_t checkpoint_id,
                       Clock::time_point now) noexcept;

    /// Alive interval tick (the daemon drives it from a WaitSet interval):
    /// a process that produced no checkpoint since the previous tick records
    /// an aliveness failure; a live process resets its failure streak.
    void on_alive_tick(std::size_t index, Clock::time_point now) noexcept;

    /// Cold-path diagnostic (mandate 9): per-process status.
    [[nodiscard]] std::string to_string() const;

private:
    struct DeadlineClock {
        std::optional<Clock::time_point> started{};
    };

    struct ProcessSupervision {
        SupervisionStatus status{SupervisionStatus::Ok};
        std::uint32_t consecutive_failures{0};
        std::uint64_t checkpoints_this_window{0};
        std::optional<std::uint32_t> last_checkpoint{};
        std::optional<Clock::time_point> last_checkpoint_time{};
        std::vector<DeadlineClock> deadline_clocks{};
    };

    void record_failure(std::size_t index) noexcept;
    void reset_failures(std::size_t index) noexcept;
    [[nodiscard]] bool is_allowed_transition(std::size_t index, std::uint32_t from,
                                             std::uint32_t to) const noexcept;

    PhmManifest manifest_;
    std::vector<ProcessSupervision> states_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const Supervisor& supervisor) {
    return supervisor.to_string();
}

} // namespace nexigen::phm

/// The supervisor formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::phm::Supervisor> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::phm::Supervisor& supervisor, Ctx& ctx) const {
        const std::string text = supervisor.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
