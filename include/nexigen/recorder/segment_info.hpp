// nexigen::recorder — manifest entry for one segment triplet.
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::recorder {

/// @brief One `.nxrec`/`.nxidx`/`.nxvid` triplet as recorded in the manifest.
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
/// @param value Value to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const SegmentInfo& segment) {
    return segment.to_string();
}

void to_json(nlohmann::json& j, const SegmentInfo& segment);
void from_json(const nlohmann::json& j, SegmentInfo& segment) noexcept;

} // namespace nexigen::recorder
