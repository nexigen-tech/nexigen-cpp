/// @file segment_info.hpp
/// @brief Manifest entry for one segment triplet.
///
/// @note Spec `recorder/raw-storage`, requirements "Session manifest and segment
///       triplet", "Rotation and retention", and "Crash recovery".
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::recorder {

/// @brief One `.nxrec`/`.nxidx`/`.nxvid` triplet as recorded in the manifest.
///
/// Paths are relative to the session directory; `closed` becomes true only after
/// a valid `SegmentFooter` is written. Retention removes whole closed entries.
///
/// @note Spec `recorder/raw-storage`, requirement "Session manifest and segment
///       triplet"; `start_commit_ns`/`end_commit_ns` use the writer's commit
///       clock, not the event time.
struct SegmentInfo {
    std::uint32_t seq{0};                 ///< 0-based segment sequence
    std::string rec_path{};               ///< relative `.nxrec` path
    std::string idx_path{};               ///< relative `.nxidx` path
    std::string vid_path{};               ///< relative `.nxvid` path
    bool closed{false};                   ///< clean footer written
    std::uint64_t records{0};             ///< records written
    std::uint64_t gaps{0};                ///< gap records written
    std::uint64_t rec_bytes{0};           ///< bytes in `.nxrec`
    std::uint64_t vid_bytes{0};           ///< bytes in `.nxvid`
    std::uint64_t start_commit_ns{0};     ///< first commit time
    std::uint64_t end_commit_ns{0};       ///< last commit time

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable segment summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `SegmentInfo::to_string`.
/// @param segment Value to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const SegmentInfo& segment) {
    return segment.to_string();
}

/// @brief Serialize a segment entry to `session.json`.
/// @param j Destination JSON object.
/// @param segment Segment entry to serialize.
void to_json(nlohmann::json& j, const SegmentInfo& segment);

/// @brief Parse a segment entry from `session.json`.
/// @param j Source JSON object.
/// @param segment Destination segment entry.
void from_json(const nlohmann::json& j, SegmentInfo& segment) noexcept;

} // namespace nexigen::recorder
