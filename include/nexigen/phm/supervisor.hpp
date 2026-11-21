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

#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/phm/em_lifecycle.hpp>
#include <nexigen/phm/fault_kind.hpp>
#include <nexigen/phm/phm_manifest.hpp>
#include <nexigen/phm/supervision_status.hpp>

namespace nexigen::phm {

/// @brief The supervision engine over a manifest's process list.
///
/// One headless state machine per supervised process, fed by checkpoint
/// activations and interval ticks:
///   * Alive counts checkpoints per window and compares the count with the
///     configured expected count and count margins (or the legacy at-least-one
///     rule when no expected count is declared);
///   * Logical validates each checkpoint against the allowed-transition graph
///     and the declared fork/join regions;
///   * Deadline times every `source -> target` pair against its min/max
///     window.
///
/// A tolerance counter drives the per-process `Ok -> Failed -> Expired`
/// machine. Every hot path is bounded: small linear scans over pre-allocated
/// vectors, no allocation, no locks, and the per-cycle cost does not depend on
/// the checkpoint flood rate. The daemon's monitoring thread is the single
/// writer, so the engine carries no synchronization; readers on other threads
/// must be externally serialized.
///
/// @note Spec `phm/supervision`, requirements "Alive supervision", "Logical
///       supervision", "Deadline supervision", "Status aggregation", and
///       "Babbling idiot protection".
class Supervisor {
public:
    /// @brief Monotonic clock every supervision timestamp is expressed in.
    using Clock = std::chrono::steady_clock;

    /// @brief Build the engine from an immutable manifest snapshot.
    ///
    /// Pre-allocates the per-process state, including one deadline clock per
    /// configured pair, so the hot path never allocates.
    ///
    /// @param manifest Manifest snapshot moved into the engine; it is the sole
    ///        source of truth for every parameter.
    /// @note Spec `phm/supervision`, requirement "Status aggregation".
    explicit Supervisor(PhmManifest manifest);

    /// @brief Number of supervised processes.
    ///
    /// @return Size of the manifest's process list.
    /// @note Spec `phm/manifest`, requirement "Declarative health manifest".
    [[nodiscard]] std::size_t process_count() const noexcept;

    /// @brief The manifest entry of one supervised process.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @return Reference to the immutable manifest entry.
    /// @note Spec `phm/manifest`, requirement "Declarative health manifest".
    /// @warning The index is not bounds-checked; pass a value below
    ///          `process_count()`.
    [[nodiscard]] const SupervisedProcess& process(std::size_t index) const noexcept;

    /// @brief Current aggregated supervision status of one process.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @return `Ok`, `Failed`, or `Expired`; `Exhausted` is only produced by
    ///         the lifecycle path when the restart budget is spent.
    /// @note Spec `phm/supervision`, requirement "Status aggregation".
    /// @warning The index is not bounds-checked.
    [[nodiscard]] SupervisionStatus status(std::size_t index) const noexcept;

    /// @brief Feed one checkpoint activation received from a process.
    ///
    /// A checkpoint arriving faster than `alive_min_margin_ms` after the
    /// previous one is ignored entirely (babbling-idiot protection, including
    /// its logical and deadline checks). Otherwise the transition from the
    /// previous checkpoint is validated against the Logical graph and a
    /// violation is recorded immediately; each Deadline pair starts its clock
    /// on `source_id` and, on `target_id`, records a failure when the elapsed
    /// milliseconds fall outside the inclusive `[min_ms, max_ms]` window. The
    /// checkpoint then becomes the previous one and counts toward the Alive
    /// window.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @param checkpoint_id Checkpoint id carried by the event activation.
    /// @param now Monotonic arrival time from `Clock::now()`.
    /// @note Spec `phm/supervision`, requirements "Logical supervision",
    ///       "Deadline supervision", and "Babbling idiot protection".
    /// @warning The index is not bounds-checked; the daemon is the single
    ///          writer.
    void on_checkpoint(std::size_t index, std::uint32_t checkpoint_id,
                       Clock::time_point now) noexcept;

    /// @brief Alive interval tick, driven by the daemon's WaitSet interval.
    ///
    /// With a configured expected count, the window fails when its checkpoint
    /// count leaves the inclusive `[expected - min, expected + max]` range.
    /// Without one, a process that produced no checkpoint since the previous
    /// tick records an aliveness failure; a live process clears its failure
    /// streak and returns to `Ok`. The window counter is reset either way.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @param now Tick time; the decision uses the window counter, so this
    ///        value is currently informational.
    /// @note Spec `phm/supervision`, requirements "Alive supervision" and
    ///       "Status aggregation". The wait itself consumes 0% CPU, per
    ///       "WaitSet-driven monitoring loop".
    /// @warning The index is not bounds-checked; the daemon is the single
    ///          writer.
    void on_alive_tick(std::size_t index, Clock::time_point now) noexcept;

    /// @brief Apply one EM execution-state notification.
    ///
    /// `Running` arms supervision and clears the failure streak and every
    /// pending deadline clock. `Terminating` and `Terminated` suspend the
    /// process: later ticks and checkpoints are ignored and the status stays
    /// `Ok`, so a deliberate stop cannot trigger recovery. `Failed` marks the
    /// process `Exhausted` and leaves it there. `Unknown` preserves the legacy
    /// behavior.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @param state Reported EM lifecycle state.
    /// @param function_group Function Group the process belongs to; empty when
    ///        the publisher could not resolve it.
    /// @note Spec `phm/supervision`, requirements "Lifecycle-armed supervision"
    ///       and "Exhaustion escalates instead of restarting".
    /// @warning The index is not bounds-checked.
    void on_execution_state(std::size_t index, EmLifecycleState state,
                            std::string_view function_group = {}) noexcept;

    /// @brief Current EM lifecycle state of one process.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @return The last applied state; `Unknown` when no notification arrived.
    [[nodiscard]] EmLifecycleState em_lifecycle(std::size_t index) const noexcept;

    /// @brief Function Group the process reported with its lifecycle events.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @return The group name, or an empty view when none was reported yet.
    /// @note Spec `phm/recovery`, requirement "SafeState escalation".
    [[nodiscard]] std::string_view function_group(std::size_t index) const noexcept;

    /// @brief Violation that last moved the process off `Ok`.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @return The recorded violation kind; `AliveFailure` before the first
    ///         failure.
    /// @note Spec `phm/recovery`, requirement "Fault storage".
    [[nodiscard]] FaultKind last_fault(std::size_t index) const noexcept;

    /// @brief Whether the process spent its restart budget.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @return `true` when the status is `Exhausted`.
    /// @note Spec `phm/supervision`, requirement "Exhaustion escalates instead
    ///       of restarting".
    [[nodiscard]] bool exhausted(std::size_t index) const noexcept;

    /// @brief Number of lifecycle notifications applied so far.
    /// @return Count of applied lifecycle notifications.
    [[nodiscard]] std::uint64_t lifecycle_notifications() const noexcept {
        return lifecycle_notifications_;
    }

    /// @brief Cold-path diagnostic listing per-process status.
    ///
    /// @return Text of the form `Supervisor([<name>=<status>, ...])`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `phm/supervision`, requirement "Supervision diagnostics".
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Per-pair deadline measurement state.
    struct DeadlineClock {
        std::optional<Clock::time_point> started{}; ///< Source arrival, unset when idle.
    };

    /// @brief One open fork region: its declaration index and entered branches.
    struct ForkFrame {
        std::size_t fork_index{0};  ///< Index into `logical_forks`.
        std::bitset<256> entered{}; ///< Branch entries reached so far.
    };

    /// @brief Length of the Function Group buffer carried by lifecycle events.
    static constexpr std::size_t k_function_group_length = 32;

    /// @brief Mutable supervision state of one process.
    struct ProcessSupervision {
        SupervisionStatus status{SupervisionStatus::Ok}; ///< Aggregated status.
        std::uint32_t consecutive_failures{0};           ///< Current failure streak.
        std::uint64_t checkpoints_this_window{0};        ///< Checkpoints since the last alive tick.
        std::optional<std::uint32_t> last_checkpoint{};  ///< Previous checkpoint id.
        std::optional<Clock::time_point> last_checkpoint_time{}; ///< Previous arrival time.
        EmLifecycleState lifecycle{EmLifecycleState::Unknown};   ///< Last EM lifecycle state.
        char function_group[k_function_group_length]{};          ///< Reported Function Group.
        FaultKind last_fault{FaultKind::AliveFailure};           ///< Last recorded violation.
        std::vector<DeadlineClock> deadline_clocks{};            ///< One clock per Deadline pair.
        std::array<ForkFrame, k_max_logical_forks> fork_stack{}; ///< Open fork regions.
        std::size_t fork_depth{0};                               ///< Number of open frames.
    };

    /// @brief Record one failure and advance the tolerance state machine.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @param kind Violation that produced the failure; recorded so the fault
    ///        journal names the real cause.
    /// @note Spec `phm/supervision`, requirement "Status aggregation": a
    ///       failure within the tolerance yields `Failed`, a streak beyond it
    ///       yields `Expired`.
    void record_failure(std::size_t index, FaultKind kind) noexcept;

    /// @brief Clear the failure streak and return the process to `Ok`.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @note Spec `phm/supervision`, requirement "Status aggregation".
    void reset_failures(std::size_t index) noexcept;

    /// @brief Whether the Logical graph allows `from -> to` for one process.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @param from Previous checkpoint id.
    /// @param to Candidate successor checkpoint id.
    /// @return `true` when the manifest lists the edge, `false` otherwise.
    /// @note Spec `phm/supervision`, requirement "Logical supervision".
    [[nodiscard]] bool is_allowed_transition(std::size_t index, std::uint32_t from,
                                             std::uint32_t to) const noexcept;

    /// @brief Track one checkpoint against the declared fork/join regions.
    ///
    /// A fork pushes a frame, a branch entry marks its bit in the innermost
    /// frame, and a join pops the matching frame after verifying that every
    /// branch was entered. An out-of-order join, a join with unentered
    /// branches, or a fork beyond the fixed stack depth records a logical
    /// failure. Processes without declared forks are untouched.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @param checkpoint_id Arriving checkpoint id.
    /// @note Spec `phm/supervision`, requirement "Logical supervision". The
    ///       work is bounded by the declared forks and branches, and allocates
    ///       nothing.
    void apply_fork_join(std::size_t index, std::uint32_t checkpoint_id) noexcept;

    PhmManifest manifest_;                     ///< Immutable configuration snapshot.
    std::vector<ProcessSupervision> states_;   ///< One mutable state per process.
    std::uint64_t lifecycle_notifications_{0}; ///< Applied lifecycle notifications.
};

/// @brief Free helper mirroring `Supervisor::to_string()` (mandate 9).
///
/// @param supervisor Engine to render.
/// @return The engine's diagnostic text.
/// @note Mandate 9; allocates, so cold path only. Spec `phm/supervision`,
///       requirement "Supervision diagnostics".
[[nodiscard]] inline std::string to_string(const Supervisor& supervisor) {
    return supervisor.to_string();
}

} // namespace nexigen::phm

/// @brief Formats a `Supervisor` as its diagnostic string.
///
/// Delegates to the member `to_string`, so `std::format("{}", supervisor)`
/// equals the member output (per-process supervision status).
///
/// @note Mandate 11 (universal `std::formatter`); cold path only. Spec
///       `phm/supervision`, requirement "Supervision diagnostics".
template <> struct std::formatter<nexigen::phm::Supervisor> : std::formatter<std::string_view> {
    /// @brief Write the supervisor's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param supervisor Engine to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::phm::Supervisor& supervisor, Ctx& ctx) const {
        const std::string text = supervisor.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
