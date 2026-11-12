// nexigen::hub — HubTree DTO + tree queries (change
// refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/hub/hub_run.hpp>
#include <nexigen/hub/hub_stream.hpp>

namespace nexigen::hub {

/// @brief The complete tree discovered under every explicit run root.
///
/// `Discovery` rebuilds this snapshot on each reconciliation scan and publishes
/// it through `GET /api/tree` and the SSE tree channel. A tree with no runs is
/// valid and means the roots contained nothing scannable.
struct HubTree {
    std::vector<HubRun> runs{}; ///< Runs sorted by canonical root path.

    /// @brief Render the tree as a diagnostic string.
    ///
    /// @return Text naming every run, suite, app, instance, and attempt;
    ///         allocates proportional to the tree size.
    /// @note Spec `hub/discovery`, requirement "Discovery types follow the
    ///       serialization contract".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;

    /// @brief True when no run was discovered.
    ///
    /// @return `runs.empty()`, a single vector-size check.
    [[nodiscard]] bool empty() const noexcept { return runs.empty(); }
};

/// @brief Free diagnostic helper mirroring `HubTree::to_string`.
///
/// @param value Tree to render.
/// @return The member `to_string()` output.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const HubTree& value) {
    return value.to_string();
}

/// @brief ADL JSON writer for the whole tree.
///
/// @param j Output JSON value replaced with the tree object.
/// @param value Tree to serialize.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const HubTree& value);

/// @brief ADL JSON reader for the whole tree.
///
/// Hostile or malformed input leaves `value` unchanged; nothing is thrown
/// under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read.
/// @param value Destination, untouched unless the whole object parses.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract" (hostile tree JSON is ignored).
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, HubTree& value) noexcept;

/// @brief Flatten every stream in a tree.
///
/// Includes streams of suite applications, standalone instances, and each
/// suite's execution_manager log.
///
/// @param tree Tree to walk.
/// @return Owning vector of descriptor copies in tree order; allocates.
/// @note Spec `hub/streaming`, requirement "Streams have stable identities".
/// @note Cold path only; used by stream reconciliation.
[[nodiscard]] std::vector<HubStream> all_streams(const HubTree& tree);

/// @brief Find one stream by its stable identity.
///
/// @param tree Tree to search.
/// @param id Stream identity to match.
/// @return Pointer to the descriptor inside `tree`, or `nullptr` when absent.
/// @warning The pointer refers to tree storage and is invalidated by the next
///          reconciliation scan or by destruction of the tree.
[[nodiscard]] const HubStream* find_stream(const HubTree& tree, std::string_view id) noexcept;

} // namespace nexigen::hub
