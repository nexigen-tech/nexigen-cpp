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

/// @brief Maximum length of the holder-requester buffer.
///
/// The buffer includes its terminating NUL, so a requester id is stored as at
/// most 31 bytes. Keeping it inline makes arbitration allocation-free.
inline constexpr std::size_t k_holder_requester_length = 32;

/// @brief The central state-machine arbiter over a manifest's Function Groups.
///
/// Builds one headless machine per group from an immutable manifest snapshot,
/// starting each group in its first declared state, plus the single
/// `MachineState` machine in `Startup`. A request is validated against the
/// group's allowed-transition graph, then arbitrated against the current
/// holder's configured priority; the accepted request becomes the new holder.
/// Startup and shutdown are explicit steps instead of graph edges.
///
/// The arbiter is confined to the SM's arbitration thread: every mutation is a
/// plain store, so there are no locks, atomics, or allocations on the hot
/// path. State names are views into the manifest storage, which must outlive
/// the manager.
///
/// @note Spec `sm/state-machine`, requirement "State machines execution".
/// @note Spec `sm/state-machine`, requirement "Request arbitration".
class StateManager {
public:
    /// @brief Build the arbiter from an immutable manifest snapshot.
    ///
    /// Every group starts in its first declared state and with no holder, so
    /// the first request only has to pass the graph check.
    ///
    /// @param manifest Manifest to take ownership of; it backs every state-name
    ///        view returned by this manager.
    explicit StateManager(SmManifest manifest);

    /// @brief Number of configured Function Groups.
    ///
    /// @return Group count taken from the manifest.
    [[nodiscard]] std::size_t group_count() const noexcept;

    /// @brief The name of one Function Group.
    ///
    /// @param index Group index in manifest order.
    /// @return View into manifest storage.
    /// @warning `index` must be below `group_count()`; no bounds check occurs.
    [[nodiscard]] std::string_view group_name(std::size_t index) const noexcept;

    /// @brief The current state of one Function Group.
    ///
    /// @param index Group index in manifest order.
    /// @return View into manifest storage, or an empty view when the group
    ///         declares no states.
    /// @warning `index` must be below `group_count()`; no bounds check occurs.
    [[nodiscard]] std::string_view state(std::size_t index) const noexcept;

    /// @brief The main machine's current state.
    ///
    /// @return Current `MachineState`, `Startup` until sequencing advances it.
    [[nodiscard]] MachineState machine_state() const noexcept { return machine_; }

    /// @brief Index of the Function Group with the given name.
    ///
    /// @param name Group name to find.
    /// @return Zero-based index, or `static_cast<std::size_t>(-1)` when no
    ///         group matches.
    [[nodiscard]] std::size_t find_group_index(std::string_view name) const noexcept;

    /// @brief True when the group declares the given state.
    ///
    /// Used by recovery and shutdown to decide whether a forced target exists.
    ///
    /// @param index Group index in manifest order.
    /// @param name State name to look for.
    /// @return `true` when `name` is one of the group's declared states.
    /// @note Spec `sm/recovery-handler`, requirement "PHM fault-event
    ///       interface" (recovery target check).
    [[nodiscard]] bool has_state(std::size_t index, std::string_view name) const noexcept;

    /// @brief The group's preparation timeout for the transition handshake.
    ///
    /// @param index Group index in manifest order.
    /// @return Configured `preparation_timeout_ms`.
    /// @note Spec `sm/ipc`, requirement "Prepare-for-transition handshake".
    [[nodiscard]] std::uint64_t preparation_timeout_ms(std::size_t index) const noexcept;

    /// @brief The group's transition watchdog bound.
    ///
    /// @param index Group index in manifest order.
    /// @return Configured `transition_max_time_ms`.
    /// @note Spec `sm/lifecycle`, requirement "Transition watchdog".
    [[nodiscard]] std::uint64_t transition_max_time_ms(std::size_t index) const noexcept;

    /// @brief Request a transition for one group.
    ///
    /// The request is validated against the group's graph first: an unknown
    /// target or a `from -> to` pair absent from the graph (including a
    /// no-op request whose target is the current state) yields `InvalidGraph`
    /// and leaves the group untouched. Otherwise the requester priority is
    /// compared with the current holder: below it yields `RejectedByPriority`;
    /// equal or higher applies the target, records the requester as the new
    /// holder, and returns `Success`.
    ///
    /// @param index Group index in manifest order.
    /// @param target Requested destination state name.
    /// @param requester_id Requester identity looked up in the priority table.
    /// @return `Success`, `InvalidGraph`, or `RejectedByPriority`; `Timeout` is
    ///         produced by the watchdog layer, not by this call.
    /// @note Spec `sm/state-machine`, requirement "State machines execution"
    ///       (an invalid transition is rejected and the group stays put).
    /// @note Spec `sm/state-machine`, requirement "Request arbitration"
    ///       (the higher-priority requester wins).
    [[nodiscard]] TransitionResult request_transition(std::size_t index, std::string_view target,
                                                      std::string_view requester_id) noexcept;

    /// @brief Startup sequencing step: `Startup` -> `Verify`.
    ///
    /// @return `true` when the machine was in `Startup` and advanced; `false`
    ///         when the step is out of order.
    /// @note Spec `sm/state-machine`, requirement "Startup and shutdown
    ///       sequencing".
    [[nodiscard]] bool advance_startup() noexcept;

    /// @brief Startup sequencing step: `Verify` -> `NormalRunning`.
    ///
    /// Called only after the execution_manager reported successful
    /// self-diagnosis.
    ///
    /// @return `true` when the machine was in `Verify` and advanced; `false`
    ///         when the step is out of order.
    /// @note Spec `sm/state-machine`, requirement "Startup and shutdown
    ///       sequencing".
    [[nodiscard]] bool confirm_startup_step() noexcept;

    /// @brief Shutdown sequencing: application groups to `Off`, then shutdown.
    ///
    /// Every group marked `application_group` is forced to `Off` (only when it
    /// declares that state), and the machine then enters
    /// `MachineState::Shutdown`.
    ///
    /// @note Spec `sm/state-machine`, requirement "Startup and shutdown
    ///       sequencing".
    void begin_shutdown() noexcept;

    /// @brief Force one group into a state, bypassing arbitration.
    ///
    /// Used by the shutdown sequence and the recovery handler: the graph and
    /// requester priorities are not consulted. An undeclared target leaves the
    /// group unchanged; the recorded holder priority and requester are not
    /// updated.
    ///
    /// @param index Group index in manifest order.
    /// @param target State name to force.
    /// @note Spec `sm/lifecycle`, requirement "Transition watchdog" (force
    ///       `SafeState` or `Off`).
    /// @note Spec `sm/recovery-handler`, requirement "PHM fault-event
    ///       interface".
    void force_state(std::size_t index, std::string_view target) noexcept;

    /// @brief Render the manager state as a diagnostic string.
    ///
    /// @return Text naming the machine state and every group's current state;
    ///         allocates.
    /// @note Spec `sm/state-machine`, requirement "State machines execution".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Per-group mutable state, separate from the immutable manifest.
    struct GroupRuntime {
        std::size_t state_index{0};                         ///< Index into `states`.
        std::int32_t holder_priority{0};                    ///< Priority of the holder.
        char holder_requester[k_holder_requester_length]{}; ///< Requester id, inline.
    };

    /// @brief Index of a declared state within one group.
    ///
    /// @param index Group index in manifest order.
    /// @param name State name to find.
    /// @return Index into the group's `states`, or its size when not declared.
    [[nodiscard]] std::size_t find_state(std::size_t index, std::string_view name) const noexcept;

    /// @brief Test one `from -> to` edge against the group's graph.
    ///
    /// @param index Group index in manifest order.
    /// @param from Source state name.
    /// @param to Destination state name.
    /// @return `true` when the exact pair is declared.
    [[nodiscard]] bool is_allowed(std::size_t index, std::string_view from,
                                  std::string_view to) const noexcept;

    SmManifest manifest_;                         ///< Immutable configuration snapshot.
    std::vector<GroupRuntime> groups_;            ///< Mutable runtime, one per group.
    MachineState machine_{MachineState::Startup}; ///< Main machine state.
};

/// @brief Free diagnostic helper mirroring `StateManager::to_string`.
///
/// @param manager Manager to render.
/// @return The member `to_string()` output.
/// @note Spec `sm/state-machine`, requirement "State machines execution".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const StateManager& manager) {
    return manager.to_string();
}

} // namespace nexigen::sm

/// @brief `std::formatter` for `StateManager` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the member
/// diagnostic, so `std::format("{}", manager)` equals `to_string()`.
///
/// @note Spec `sm/state-machine`, requirement "State machines execution".
template <> struct std::formatter<nexigen::sm::StateManager> : std::formatter<std::string_view> {
    /// @brief Format the manager as its diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param manager Manager to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx> auto format(const nexigen::sm::StateManager& manager, Ctx& ctx) const {
        const std::string text = manager.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
