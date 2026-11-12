// nexigen::phm — declarative health manifest (change add-phm; spec
// phm/manifest; design D7).
//
// The JSON configuration the daemon reads at startup: supervised processes
// with their iceoryx2 service identifiers, Alive parameters, Logical
// transition graphs, Deadline pairs, and Recovery rules. Parsed once into an
// immutable config; JSON only (nlohmann, mandate 12) — YAML input is a future
// converter.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>
#include <nexigen/phm/recovery_action.hpp>

namespace nexigen::phm {

/// @brief One allowed checkpoint transition in the logical-supervision graph.
///
/// A directed edge `from_id -> to_id`: the supervisor accepts a checkpoint only
/// when an edge exists from the previous checkpoint to it. The graph is a plain
/// edge list, so the hot-path lookup is a small linear scan.
///
/// @note Spec `phm/supervision`, requirement "Logical supervision": ordering is
///       validated against a directed graph of allowed transitions.
struct CheckpointTransition {
    std::uint32_t from_id{0}; ///< Predecessor checkpoint id.
    std::uint32_t to_id{0};   ///< Allowed successor checkpoint id.

    /// @brief Cold-path diagnostic naming both ends of the edge.
    ///
    /// @return Text of the form `CheckpointTransition(from=<id>, to=<id>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `phm/supervision`, requirement "Logical supervision".
    [[nodiscard]] std::string to_string() const;
};

/// @brief One deadline-supervision pair: source -> target with min/max limits.
///
/// The supervisor starts a clock when `source_id` arrives and, when
/// `target_id` later arrives, compares the elapsed milliseconds against the
/// inclusive `[min_ms, max_ms]` window.
///
/// @note Spec `phm/supervision`, requirement "Deadline supervision".
struct DeadlinePair {
    std::uint32_t source_id{0}; ///< Checkpoint that starts the measurement.
    std::uint32_t target_id{0}; ///< Checkpoint that stops it.
    std::uint64_t min_ms{0};    ///< Inclusive lower bound on the elapsed time.
    std::uint64_t max_ms{0};    ///< Inclusive upper bound on the elapsed time.

    /// @brief Cold-path diagnostic naming the pair and its limits.
    ///
    /// @return Text of the form
    ///         `DeadlinePair(source=<id>, target=<id>, min_ms=<n>, max_ms=<n>)`.
    /// @note Mandate 9; allocates, so cold path only. Spec `phm/supervision`,
    ///       requirement "Deadline supervision".
    [[nodiscard]] std::string to_string() const;
};

/// @brief One recovery rule attached to an `Expired` process.
///
/// The daemon applies the first rule of a process; an empty rule list escalates
/// the safety-critical default to the State Manager.
///
/// @note Spec `phm/recovery`, requirements "Local process recovery" and
///       "SafeState escalation"; Spec `phm/manifest`, requirement "Declarative
///       health manifest".
struct RecoveryRule {
    RecoveryAction action{RecoveryAction::RestartProcess}; ///< Strategy on `Expired`.
    std::string function_group{}; ///< Only used by SwitchFunctionGroupState.

    /// @brief Cold-path diagnostic naming the action and function group.
    ///
    /// @return Text of the form
    ///         `RecoveryRule(action=<action>, function_group=<group>)`.
    /// @note Mandate 9; allocates, so cold path only. Spec `phm/manifest`,
    ///       requirement "Declarative health manifest".
    [[nodiscard]] std::string to_string() const;
};

/// @brief One supervised process: identity plus every supervision parameter.
///
/// The manifest entry binds a process name to its iceoryx2 checkpoint service
/// and carries the three supervision parameters (Alive window with margins,
/// Logical transition graph, Deadline pairs) plus the tolerance counter and the
/// recovery rules.
///
/// @note Spec `phm/manifest`, requirement "Declarative health manifest". Spec
///       `phm/supervision`, requirements "Alive supervision", "Logical
///       supervision", "Deadline supervision", and "Status aggregation".
struct SupervisedProcess {
    std::string name{};         ///< Application name used in diagnostics.
    std::string service_name{}; ///< iceoryx2 service identifier (checkpoints channel).
    std::uint64_t alive_reference_cycle_ms{0};            ///< Alive window length.
    std::uint64_t alive_min_margin_ms{0};                 ///< Minimum spacing between checkpoints.
    std::uint64_t alive_max_margin_ms{0};                 ///< Maximum allowed gap in the window.
    std::uint64_t failed_supervision_cycles_tolerance{0}; ///< Consecutive failures allowed.
    bool safety_critical{false};                          ///< Escalate directly to SafeState.
    std::vector<std::uint32_t> checkpoints{};             ///< Expected checkpoint ids (alive).
    std::vector<CheckpointTransition> transitions{};      ///< Logical graph edges.
    std::vector<DeadlinePair> deadlines{};                ///< Deadline pairs with limits.
    std::vector<RecoveryRule> recovery_rules{};           ///< Actions on Expired.

    /// @brief Cold-path diagnostic naming the process and its parameters.
    ///
    /// @return Text listing the process identity and configured supervision
    ///         data.
    /// @note Mandate 9; allocates, so cold path only. Spec `phm/manifest`,
    ///       requirement "Declarative health manifest".
    [[nodiscard]] std::string to_string() const;
};

/// @brief The health-logic manifest the daemon loads at startup.
///
/// Parsed once into an immutable configuration: the supervised process list
/// plus the thread specs used by infrastructure threads. JSON only (nlohmann,
/// mandate 12); YAML input is a future converter. Serialization and diagnostics
/// are cold-path operations.
///
/// @note Spec `phm/manifest`, requirement "Declarative health manifest". Spec
///       `phm/manifest`, requirement "PHM manifest carries background thread
///       specs".
struct PhmManifest {
    std::vector<SupervisedProcess> processes{}; ///< Supervised process list.
    core::ThreadSpec bg_thread{};               ///< fallback for infrastructure threads
    core::ThreadSpec fault_storage_thread{};    ///< fault-log writer spec

    /// @brief Cold-path diagnostic listing the supervised processes.
    ///
    /// @return Text naming every process plus both thread specs.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `phm/manifest`, requirement "Manifest diagnostics".
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `PhmManifest::to_string()` (mandate 9).
///
/// @param manifest Manifest to render.
/// @return The manifest's diagnostic text.
/// @note Mandate 9; allocates, so cold path only. Spec `phm/manifest`,
///       requirement "Manifest diagnostics".
[[nodiscard]] inline std::string to_string(const PhmManifest& manifest) {
    return manifest.to_string();
}

/// @brief ADL JSON writer for one `CheckpointTransition`.
///
/// @param j Destination JSON value; overwritten with an object.
/// @param transition Transition to encode.
/// @note Mandate 10 (ADL JSON pair); cold path only. Spec `phm/manifest`,
///       requirement "Manifest serialization".
void to_json(nlohmann::json& j, const CheckpointTransition& transition);

/// @brief ADL JSON writer for one `DeadlinePair`.
///
/// @param j Destination JSON value; overwritten with an object.
/// @param pair Pair to encode.
/// @note Mandate 10; cold path only. Spec `phm/manifest`, requirement
///       "Manifest serialization".
void to_json(nlohmann::json& j, const DeadlinePair& pair);

/// @brief ADL JSON writer for one `RecoveryRule`.
///
/// The action is encoded by its canonical `to_string` name.
///
/// @param j Destination JSON value; overwritten with an object.
/// @param rule Rule to encode.
/// @note Mandate 10; cold path only. Spec `phm/manifest`, requirement
///       "Manifest serialization".
void to_json(nlohmann::json& j, const RecoveryRule& rule);

/// @brief ADL JSON writer for one `SupervisedProcess`.
///
/// @param j Destination JSON value; overwritten with an object.
/// @param process Process entry to encode.
/// @note Mandate 10; cold path only. Spec `phm/manifest`, requirement
///       "Manifest serialization".
void to_json(nlohmann::json& j, const SupervisedProcess& process);

/// @brief ADL JSON writer for one `PhmManifest`.
///
/// @param j Destination JSON value; overwritten with an object.
/// @param manifest Manifest to encode.
/// @note Mandate 10; cold path only. Spec `phm/manifest`, requirement
///       "Manifest serialization".
void to_json(nlohmann::json& j, const PhmManifest& manifest);

/// @brief ADL JSON reader for one `CheckpointTransition`.
///
/// Strictly non-throwing; a malformed field leaves the corresponding member
/// unchanged.
///
/// @param j Source JSON value; a non-object is ignored.
/// @param transition Receives the parsed fields; unchanged on malformed input.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec `phm/manifest`,
///       requirement "Manifest serialization".
void from_json(const nlohmann::json& j, CheckpointTransition& transition) noexcept;

/// @brief ADL JSON reader for one `DeadlinePair`.
///
/// Strictly non-throwing; a malformed field leaves the corresponding member
/// unchanged.
///
/// @param j Source JSON value; a non-object is ignored.
/// @param pair Receives the parsed fields; unchanged on malformed input.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec `phm/manifest`,
///       requirement "Manifest serialization".
void from_json(const nlohmann::json& j, DeadlinePair& pair) noexcept;

/// @brief ADL JSON reader for one `RecoveryRule`.
///
/// Strictly non-throwing; an unknown action name leaves the current action
/// unchanged.
///
/// @param j Source JSON value; a non-object is ignored.
/// @param rule Receives the parsed fields; unchanged on malformed input.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec `phm/manifest`,
///       requirement "Manifest serialization".
void from_json(const nlohmann::json& j, RecoveryRule& rule) noexcept;

/// @brief ADL JSON reader for one `SupervisedProcess`.
///
/// Strictly non-throwing; identity fields are read first, and a rejected
/// `checkpoints` array stops the parse so later collections keep their current
/// values.
///
/// @param j Source JSON value; a non-object is ignored.
/// @param process Receives the parsed fields; malformed fields keep their
///        previous values.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec `phm/manifest`,
///       requirement "Manifest serialization".
void from_json(const nlohmann::json& j, SupervisedProcess& process) noexcept;

/// @brief ADL JSON reader for one `PhmManifest`.
///
/// Strictly non-throwing; the process array is collected into a temporary and
/// replaces `manifest.processes` only when every element converts, and a
/// malformed thread-spec object leaves that spec unchanged.
///
/// @param j Source JSON value; a non-object is ignored.
/// @param manifest Receives the parsed configuration; malformed collections
///        keep their previous values.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec `phm/manifest`,
///       requirements "Manifest serialization" and "PHM manifest carries
///       background thread specs".
void from_json(const nlohmann::json& j, PhmManifest& manifest) noexcept;

} // namespace nexigen::phm

/// @brief Formats a `PhmManifest` as its diagnostic string.
///
/// Delegates to the member `to_string`, so `std::format("{}", manifest)` equals
/// the member output (listing the supervised processes).
///
/// @note Mandate 11 (universal `std::formatter`); cold path only. Spec
///       `phm/manifest`, requirement "Manifest diagnostics".
template <> struct std::formatter<nexigen::phm::PhmManifest> : std::formatter<std::string_view> {
    /// @brief Write the manifest's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param manifest Manifest to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::phm::PhmManifest& manifest, Ctx& ctx) const {
        const std::string text = manifest.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
