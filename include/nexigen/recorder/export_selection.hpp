/// @file export_selection.hpp
/// @brief Converter selection value object.
///
/// @note Spec `recorder/export`, requirements "Dataset selection and extraction"
///       and "Time-range and channel selection"; the selection is serialized in
///       tests and CLI configuration and does not read the session by itself.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexigen::recorder {

/// @brief One converter request: datasets/channels plus an event-time window.
///
/// `dataset` selects one dataset by id or name; `channels` adds further
/// selections (ids, names, or topics). An empty selection means "no dataset",
/// which the converter rejects. Start/end are inclusive and only applied when
/// their `has_*` flag is set.
///
/// @note Spec `recorder/export`, requirement "Time-range and channel selection";
///       the converter uses the sparse index to skip records outside the window.
struct ExportSelection {
    std::string dataset{};                ///< primary dataset id or name
    std::vector<std::string> channels{};  ///< additional dataset/channel patterns
    std::uint64_t start_event_ns{0};      ///< inclusive start when `has_start`
    std::uint64_t end_event_ns{0};        ///< inclusive end when `has_end`
    bool has_start{false};                ///< enables the start bound
    bool has_end{false};                  ///< enables the end bound

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable summary of the selected datasets and range.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `ExportSelection::to_string`.
/// @param selection Selection to render.
/// @return Human-readable selection summary.
[[nodiscard]] inline std::string to_string(const ExportSelection& selection) {
    return selection.to_string();
}

/// @brief Serialize a converter selection to JSON.
/// @param j Destination JSON object.
/// @param selection Selection to serialize.
void to_json(nlohmann::json& j, const ExportSelection& selection);

/// @brief Parse a converter selection from JSON.
/// @param j Source JSON object.
/// @param selection Destination selection.
void from_json(const nlohmann::json& j, ExportSelection& selection) noexcept;

} // namespace nexigen::recorder
