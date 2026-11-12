// nexigen::hub — StreamRecord DTO (change refactor-hub-model-config-headers).
//
// COLD PATH ONLY: used by the hub control loop, HTTP snapshots, and stream
// replay; never runs in an application hot path.
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/hub/stream_kind.hpp>

namespace nexigen::hub {

/// @brief One complete line from a stream.
///
/// The tailer emits a record only for a newline-terminated line, assigns the
/// next per-stream sequence, and stamps the receive time. An oversized line is
/// discarded rather than buffered; the following sequence gap records the loss.
/// The structure owns its strings and is copied into replay rings.
struct StreamRecord {
    std::string stream_id{};           ///< Stable stream identity.
    StreamKind kind{StreamKind::App};  ///< Stream family of the source file.
    std::uint64_t sequence{0};         ///< Monotonic per-stream sequence, starting at 1.
    std::string received_at_rfc3339{}; ///< Hub receive timestamp in RFC 3339 form.
    std::string line{};                ///< Complete line without its newline.
    bool truncated{false};             ///< Set when the line was cut to the size bound.

    /// @brief Render the record as a diagnostic string.
    ///
    /// @return Text naming stream, sequence, kind, timestamp, and line;
    ///         allocates.
    /// @note Spec `hub/streaming`, requirement "Streaming types follow the
    ///       serialization contract".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `StreamRecord::to_string`.
///
/// @param value Record to render.
/// @return The member `to_string()` output.
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const StreamRecord& value) {
    return value.to_string();
}

/// @brief ADL JSON writer for a stream record.
///
/// @param j Output JSON value replaced with the record object.
/// @param value Record to serialize.
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const StreamRecord& value);

/// @brief ADL JSON reader for a stream record.
///
/// Hostile or malformed input leaves `value` unchanged; nothing is thrown
/// under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read.
/// @param value Destination, untouched unless the whole object parses.
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract" (hostile stream JSON is ignored).
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, StreamRecord& value) noexcept;

} // namespace nexigen::hub
