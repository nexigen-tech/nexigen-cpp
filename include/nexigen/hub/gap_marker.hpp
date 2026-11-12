// nexigen::hub — GapMarker DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::hub {

/// @brief A missing sequence range in a stream ring.
///
/// A gap is reported when an oversized line is discarded, and when a
/// subscriber's sequence cursor is older than the oldest record still retained
/// in the bounded ring. `dropped` counts the missing records and the two
/// sequence fields delimit the missing span inclusively. A gap for a single
/// discarded line carries the same first and last sequence.
///
/// @note Spec `hub/streaming`, requirement "Rings are bounded per stream"
///       (a gap names the dropped count and the missing sequence range).
/// @note Spec `hub/streaming`, requirement "Lines are framed safely"
///       (an oversized line is surfaced as a gap).
struct GapMarker {
    std::string stream_id{};                 ///< Identity of the affected stream.
    std::uint64_t dropped{0};                ///< Number of missing records.
    std::uint64_t first_missing_sequence{0}; ///< First sequence no longer available.
    std::uint64_t last_missing_sequence{0};  ///< Last sequence no longer available.

    /// @brief Render the marker as a diagnostic string.
    ///
    /// @return Text naming the stream and the missing range; allocates.
    /// @note Spec `hub/streaming`, requirement "Streaming types follow the
    ///       serialization contract".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `GapMarker::to_string`.
///
/// @param value Marker to render.
/// @return The member `to_string()` output.
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const GapMarker& value) {
    return value.to_string();
}

/// @brief ADL JSON writer for a gap marker.
///
/// @param j Output JSON value replaced with the marker object.
/// @param value Marker to serialize.
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const GapMarker& value);

/// @brief ADL JSON reader for a gap marker.
///
/// Hostile or malformed input (a non-object, or wrong field types) leaves
/// `value` unchanged; nothing is thrown under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read.
/// @param value Destination, untouched unless the whole object parses.
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract" (hostile JSON is ignored).
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, GapMarker& value) noexcept;

} // namespace nexigen::hub
