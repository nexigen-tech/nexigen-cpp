// nexigen::sm — declarative state-management manifest (change add-sm;
// spec sm/manifest; design D8).
//
// The JSON configuration the SM reads at startup: Function Groups with their
// states and allowed transitions, transition/preparation timeouts, requester
// priorities, and the metrics flush interval. Parsed once into immutable
// structures; JSON only (nlohmann, mandate 12).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>

namespace nexigen::sm {

/// @brief One allowed transition of a Function Group graph.
///
/// The pair `from` -> `to` is matched state-name-exact; any request whose
/// current/target pair is absent from the list is rejected with
/// `TransitionResult::InvalidGraph`.
///
/// @note Spec `sm/manifest`, requirement "Declarative state-management
///       manifest".
struct AllowedTransition {
    std::string from{}; ///< Source state name.
    std::string to{};   ///< Destination state name.

    /// @brief Render the transition as a diagnostic string.
    ///
    /// @return Text in `from -> to` form; allocates.
    /// @note Spec `sm/manifest`, requirement "Manifest diagnostics".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief One Function Group: states, transition graph, and timeouts.
///
/// The first declared state is the group's initial state. `application_group`
/// marks the groups that shutdown sequencing moves to `Off`.
///
/// @note Spec `sm/manifest`, requirement "Declarative state-management
///       manifest".
struct FunctionGroup {
    std::string name{};                                   ///< Unique group name.
    std::vector<std::string> states{};                    ///< Declared states, first is initial.
    std::vector<AllowedTransition> allowed_transitions{}; ///< Permitted graph edges.
    std::uint64_t transition_max_time_ms{1000};           ///< Transition watchdog bound.
    std::uint64_t preparation_timeout_ms{500};            ///< Handshake ack window.
    bool application_group{true}; ///< Moved to Off during shutdown sequencing.

    /// @brief Render the group as a diagnostic string.
    ///
    /// @return Text naming the group, states, and graph; allocates.
    /// @note Spec `sm/manifest`, requirement "Manifest diagnostics".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief One requester-priority entry (higher value = higher priority).
///
/// Arbitration compares the requesting id's priority with the current holder's
/// priority; a request below the holder is rejected with
/// `TransitionResult::RejectedByPriority`.
///
/// @note Spec `sm/state-machine`, requirement "Request arbitration".
struct RequesterPriority {
    std::string requester_id{}; ///< Requester identity carried by the trigger.
    std::int32_t priority{0};   ///< Priority, higher wins.

    /// @brief Render the entry as a diagnostic string.
    ///
    /// @return Text naming the requester and its priority; allocates.
    /// @note Spec `sm/manifest`, requirement "Manifest diagnostics".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief The state-management manifest the SM loads at startup.
///
/// Parsed once from JSON into immutable structures and then read only; the
/// arbitration hot path performs no allocation and only linear scans these
/// vectors. Thread specs select placement for infrastructure workers: the
/// audit worker applies `core::effective_thread_spec(audit_thread, bg_thread)`,
/// so an empty `audit_thread` inherits `bg_thread` and two empty specs keep the
/// inherited attributes.
///
/// @note Spec `sm/manifest`, requirement "Declarative state-management
///       manifest".
/// @note Spec `sm/manifest`, requirement "SM manifest carries background
///       thread specs".
struct SmManifest {
    std::vector<FunctionGroup> function_groups{};          ///< Groups in graph order.
    std::vector<RequesterPriority> requester_priorities{}; ///< Requester priority table.
    std::uint64_t metrics_flush_interval_ms{1000};         ///< Metrics publish period.
    std::uint64_t startup_timeout_ms{5000};                ///< Readiness report budget.
    core::ThreadSpec bg_thread{};                          ///< Fallback for infrastructure threads.
    core::ThreadSpec audit_thread{};                       ///< Transition-audit writer spec.

    /// @brief Render the manifest as a diagnostic string.
    ///
    /// @return Text naming the Function Groups and their rules; allocates.
    /// @note Spec `sm/manifest`, requirement "Manifest diagnostics".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;

    /// @brief Cold-path lookup of one requester's priority.
    ///
    /// @param requester_id Requester identity to look up.
    /// @return Configured priority, or `0` when the id is unknown.
    /// @note Spec `sm/state-machine`, requirement "Request arbitration".
    [[nodiscard]] std::int32_t priority_of(std::string_view requester_id) const noexcept;
};

/// @brief Free diagnostic helper mirroring `SmManifest::to_string`.
///
/// @param manifest Manifest to render.
/// @return The member `to_string()` output.
/// @note Spec `sm/manifest`, requirement "Manifest diagnostics".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const SmManifest& manifest) {
    return manifest.to_string();
}

/// @brief ADL JSON writer for one graph edge.
///
/// @param j Output JSON value replaced with the edge object.
/// @param transition Edge to serialize.
/// @note Spec `sm/manifest`, requirement "Manifest serialization".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const AllowedTransition& transition);

/// @brief ADL JSON writer for one Function Group.
///
/// @param j Output JSON value replaced with the group object.
/// @param group Group to serialize.
/// @note Spec `sm/manifest`, requirement "Manifest serialization".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const FunctionGroup& group);

/// @brief ADL JSON writer for one requester-priority entry.
///
/// @param j Output JSON value replaced with the entry object.
/// @param priority Entry to serialize.
/// @note Spec `sm/manifest`, requirement "Manifest serialization".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const RequesterPriority& priority);

/// @brief ADL JSON writer for the whole manifest.
///
/// @param j Output JSON value replaced with the manifest object.
/// @param manifest Manifest to serialize.
/// @note Spec `sm/manifest`, requirement "Manifest serialization".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const SmManifest& manifest);

/// @brief ADL JSON reader for one graph edge, non-throwing.
///
/// Malformed input leaves `transition` unchanged under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read.
/// @param transition Destination, untouched unless the whole object parses.
/// @note Spec `sm/manifest`, requirement "Manifest serialization" (a hostile
///       input leaves the parsed configuration unchanged).
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, AllowedTransition& transition) noexcept;

/// @brief ADL JSON reader for one Function Group, non-throwing.
///
/// Malformed input leaves `group` unchanged under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read.
/// @param group Destination, untouched unless the whole object parses.
/// @note Spec `sm/manifest`, requirement "Manifest serialization".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, FunctionGroup& group) noexcept;

/// @brief ADL JSON reader for one priority entry, non-throwing.
///
/// Malformed input leaves `priority` unchanged under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read.
/// @param priority Destination, untouched unless the whole object parses.
/// @note Spec `sm/manifest`, requirement "Manifest serialization".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, RequesterPriority& priority) noexcept;

/// @brief ADL JSON reader for the whole manifest, non-throwing.
///
/// Malformed input leaves `manifest` unchanged under `JSON_NOEXCEPTION=1`;
/// thread-spec fields participate in the round-trip like every other field.
///
/// @param j JSON value to read.
/// @param manifest Destination, untouched unless the whole object parses.
/// @note Spec `sm/manifest`, requirement "Manifest serialization".
/// @note Spec `sm/manifest`, requirement "SM manifest carries background
///       thread specs" (thread specs round-trip through JSON).
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, SmManifest& manifest) noexcept;

} // namespace nexigen::sm

/// @brief `std::formatter` for `SmManifest` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the member
/// diagnostic, so `std::format("{}", manifest)` equals `to_string()`.
///
/// @note Spec `sm/manifest`, requirement "Manifest diagnostics".
template <> struct std::formatter<nexigen::sm::SmManifest> : std::formatter<std::string_view> {
    /// @brief Format the manifest as its diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param manifest Manifest to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx> auto format(const nexigen::sm::SmManifest& manifest, Ctx& ctx) const {
        const std::string text = manifest.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
