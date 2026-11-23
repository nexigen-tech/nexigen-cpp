#include "minimap.hpp"

#include <format>

namespace nexigen::logview {

namespace {
const MinimapBucket kEmptyBucket{};
} // namespace

void Minimap::on_ingest(std::uint64_t seq, nexigen::core::LogLevel level) noexcept {
    MinimapBucket& bucket = buckets_[bucket_for(seq)];
    if (level == nexigen::core::LogLevel::Error) {
        ++bucket.errors;
    } else if (level == nexigen::core::LogLevel::Fatal) {
        ++bucket.fatals;
    }
}

void Minimap::on_evict(std::uint64_t seq, nexigen::core::LogLevel level, bool bookmarked) noexcept {
    MinimapBucket& bucket = buckets_[bucket_for(seq)];
    if (level == nexigen::core::LogLevel::Error && bucket.errors > 0U) {
        --bucket.errors;
    } else if (level == nexigen::core::LogLevel::Fatal && bucket.fatals > 0U) {
        --bucket.fatals;
    }
    if (bookmarked && bucket.bookmarks > 0U) {
        --bucket.bookmarks;
    }
}

void Minimap::on_bookmark(std::uint64_t seq, bool bookmarked) noexcept {
    MinimapBucket& bucket = buckets_[bucket_for(seq)];
    if (bookmarked) {
        ++bucket.bookmarks;
    } else if (bucket.bookmarks > 0U) {
        --bucket.bookmarks;
    }
}

const MinimapBucket& Minimap::at(std::size_t index) const noexcept {
    return index < bucket_count ? buckets_[index] : kEmptyBucket;
}

std::string Minimap::to_string() const {
    std::uint64_t errors = 0;
    std::uint64_t fatals = 0;
    std::uint64_t bookmarks = 0;
    for (const MinimapBucket& bucket : buckets_) {
        errors += bucket.errors;
        fatals += bucket.fatals;
        bookmarks += bucket.bookmarks;
    }
    return std::format("Minimap(errors={}, fatals={}, bookmarks={})", errors, fatals, bookmarks);
}

std::string to_string(const Minimap& minimap) {
    return minimap.to_string();
}

} // namespace nexigen::logview
