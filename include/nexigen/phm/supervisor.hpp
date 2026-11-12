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

/// @brief The supervision engine over a manifest's process list.
///
/// One headless state machine per supervised process, fed by checkpoint
/// activations and interval ticks:
///   * Alive counts checkpoints per window and records a failure when a tick
///     finds none;
///   * Logical validates each checkpoint against the allowed-transition graph;
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
    /// @return `Ok`, `Failed`, or `Expired`; `Stopped` belongs to the
    ///         vocabulary but is not produced by the checkpoint/tick paths of
    ///         this engine.
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
    /// A process that produced no checkpoint since the previous tick records an
    /// aliveness failure; a live process clears its failure streak and returns
    /// to `Ok`. The window counter is reset either way.
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

    /// @brief Mutable supervision state of one process.
    struct ProcessSupervision {
        SupervisionStatus status{SupervisionStatus::Ok}; ///< Aggregated status.
        std::uint32_t consecutive_failures{0};           ///< Current failure streak.
        std::uint64_t checkpoints_this_window{0};        ///< Checkpoints since the last alive tick.
        std::optional<std::uint32_t> last_checkpoint{};  ///< Previous checkpoint id.
        std::optional<Clock::time_point> last_checkpoint_time{}; ///< Previous arrival time.
        std::vector<DeadlineClock> deadline_clocks{};            ///< One clock per Deadline pair.
    };

    /// @brief Record one failure and advance the tolerance state machine.
    ///
    /// @param index Process index in `[0, process_count())`.
    /// @note Spec `phm/supervision`, requirement "Status aggregation": a
    ///       failure within the tolerance yields `Failed`, a streak beyond it
    ///       yields `Expired`.
    void record_failure(std::size_t index) noexcept;

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

    PhmManifest manifest_;                   ///< Immutable configuration snapshot.
    std::vector<ProcessSupervision> states_; ///< One mutable state per process.
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
