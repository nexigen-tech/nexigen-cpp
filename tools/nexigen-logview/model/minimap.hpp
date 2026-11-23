// nexigen-logview — fixed 1024-bucket severity/bookmark minimap.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include <nexigen/core/log_level.hpp>

namespace nexigen::logview {

/// @brief Fixed count bucket covering one contiguous slice of history.
struct MinimapBucket {
    std::uint32_t errors{0};    ///< Error records in the bucket.
    std::uint32_t fatals{0};    ///< Fatal records in the bucket.
    std::uint32_t bookmarks{0}; ///< Bookmarked records in the bucket.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the three counters.
    [[nodiscard]] std::string to_string() const {
        return std::format("MinimapBucket(errors={}, fatals={}, bookmarks={})", errors, fatals,
                           bookmarks);
    }
};

/// @brief Counts severity and bookmark density across the retained history.
class Minimap {
public:
    /// @brief Number of buckets in the fixed minimap.
    static constexpr std::size_t bucket_count = 1024;

    /// @brief Clear every bucket.
    void clear() noexcept { buckets_.fill(MinimapBucket{}); }

    /// @brief Map an absolute record number onto its bucket.
    /// @param seq Absolute record number.
    /// @return Bucket index in `0..1023`.
    [[nodiscard]] static constexpr std::size_t bucket_for(std::uint64_t seq) noexcept {
        return seq & (bucket_count - 1U);
    }

    /// @brief Account for one ingested record.
    /// @param seq Absolute record number.
    /// @param level Record severity.
    void on_ingest(std::uint64_t seq, nexigen::core::LogLevel level) noexcept;

    /// @brief Account for one evicted record.
    /// @param seq Absolute record number.
    /// @param level Record severity.
    /// @param bookmarked Whether the record was bookmarked.
    void on_evict(std::uint64_t seq, nexigen::core::LogLevel level, bool bookmarked) noexcept;

    /// @brief Account for a bookmark toggle.
    /// @param seq Absolute record number.
    /// @param bookmarked New bookmark state.
    void on_bookmark(std::uint64_t seq, bool bookmarked) noexcept;

    /// @brief Read one bucket.
    /// @param index Bucket index; out-of-range returns an empty bucket.
    /// @return Borrowed bucket.
    [[nodiscard]] const MinimapBucket& at(std::size_t index) const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with aggregate error, fatal, and bookmark counts.
    [[nodiscard]] std::string to_string() const;

private:
    std::array<MinimapBucket, bucket_count> buckets_{}; ///< Fixed counters.
};

/// @brief Free diagnostic mirror of `Minimap::to_string`.
/// @param minimap Minimap to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const Minimap& minimap);

} // namespace nexigen::logview
