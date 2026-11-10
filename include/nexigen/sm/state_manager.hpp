// nexigen::sm — central arbitration engine (change add-sm; spec
// sm/state-machine; design D3).
//
// Headless per-group state machines plus the one MachineState machine. A
// request is checked against the group's transition graph, then arbitrated
// against the current holder's priority; the highest requester wins. Startup
// and shutdown are explicit sequencing steps. All hot paths are O(1) in the
// manifest size (small linear scans over pre-allocated vectors, no
// allocations, no locks — the arbitration thread is the single writer).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/sm/machine_state.hpp>
#include <nexigen/sm/sm_manifest.hpp>
#include <nexigen/sm/transition_result.hpp>

namespace nexigen::sm {

/// Maximum length of the holder-requester buffer (allocation-free arbitration).
inline constexpr std::size_t k_holder_requester_length = 32;

/// The central state-machine arbiter over a manifest's Function Groups.
class StateManager {
public:
    /// Build the arbiter from an immutable manifest snapshot. Every group
    /// starts in its first declared state.
    explicit StateManager(SmManifest manifest);

    /// Number of configured Function Groups.
    [[nodiscard]] std::size_t group_count() const noexcept;

    /// The name of one Function Group.
    [[nodiscard]] std::string_view group_name(std::size_t index) const noexcept;

    /// The current state of one Function Group.
    [[nodiscard]] std::string_view state(std::size_t index) const noexcept;

    /// The main machine's current state.
    [[nodiscard]] MachineState machine_state() const noexcept { return machine_; }

    /// Index of the Function Group with `name`; npos when unknown.
    [[nodiscard]] std::size_t find_group_index(std::string_view name) const noexcept;

    /// True when the group declares the state `name` (recovery target check).
    [[nodiscard]] bool has_state(std::size_t index, std::string_view name) const noexcept;

    /// The group's preparation timeout (ms) for the transition handshake.
    [[nodiscard]] std::uint64_t preparation_timeout_ms(std::size_t index) const noexcept;

    /// The group's transition watchdog bound (ms).
    [[nodiscard]] std::uint64_t transition_max_time_ms(std::size_t index) const noexcept;

    /// Request a transition for one group. The request is validated against
    /// the transition graph (`InvalidGraph`), then arbitrated against the
    /// current holder (`RejectedByPriority` when a higher-priority requester
    /// holds the group); otherwise the state is applied (`Success`).
    [[nodiscard]] TransitionResult request_transition(std::size_t index,
                                                      std::string_view target,
                                                      std::string_view requester_id) noexcept;

    /// Startup sequencing step: Startup -> Verify.
    [[nodiscard]] bool advance_startup() noexcept;

    /// Startup sequencing step: Verify -> NormalRunning (the EM reported
    /// successful self-diagnosis).
    [[nodiscard]] bool confirm_startup_step() noexcept;

    /// Shutdown sequencing: every application group moves to `Off`, then the
    /// machine enters `Shutdown`.
    void begin_shutdown() noexcept;

    /// Force one group into a state, bypassing arbitration (recovery path).
    void force_state(std::size_t index, std::string_view target) noexcept;

    /// Cold-path diagnostic (mandate 9): machine state + per-group states.
    [[nodiscard]] std::string to_string() const;

private:
    struct GroupRuntime {
        std::size_t state_index{0};
        std::int32_t holder_priority{0};
        char holder_requester[k_holder_requester_length]{};
    };

    [[nodiscard]] std::size_t find_state(std::size_t index, std::string_view name) const noexcept;
    [[nodiscard]] bool is_allowed(std::size_t index, std::string_view from,
                                  std::string_view to) const noexcept;

    SmManifest manifest_;
    std::vector<GroupRuntime> groups_;
    MachineState machine_{MachineState::Startup};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const StateManager& manager) {
    return manager.to_string();
}

} // namespace nexigen::sm

/// The state manager formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::sm::StateManager> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::sm::StateManager& manager, Ctx& ctx) const {
        const std::string text = manager.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
