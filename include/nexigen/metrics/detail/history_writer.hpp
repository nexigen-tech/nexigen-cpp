// nexigen::metrics — quota-bounded JSON-lines history writer (change
// add-metrics-daemon; spec metrics/metrics-daemon "Daemon writes a
// quota-bounded JSON-lines history"; design D7).
//
// One JSON line per flushed window. Cold path (once per flush interval): the
// line is built with nlohmann/json, which is permitted here and never on a
// real-time path. Rotation renames the active file to `<path>.1` and starts
// fresh once the quota is reached, bounding storage to ~2x the quota.
#pragma once

#include <cerrno>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/metrics/metric_batch_snapshot.hpp>
#include <nexigen/metrics/metric_frame.hpp>

namespace nexigen::metrics::detail {

/// Serialize one window of frames as a `MetricBatchSnapshot` JSON object (no
/// trailing newline; the caller appends it). The line carries `stamp_ns`,
/// `utc_ns` (the window stamp converted with `utc_offset_ns`), `record_count`,
/// and the `records` array, matching the existing snapshot serialization.
[[nodiscard]] inline std::string build_history_line(const MetricFrame* frames, std::size_t count,
                                                    std::int64_t utc_offset_ns = 0) {
    std::vector<MetricRecord> records(count);
    for (std::size_t i = 0; i < count; ++i) {
        records[i] = frames[i].record;
    }
    MetricBatchSnapshot batch{};
    if (count > 0U) {
        batch.stamp = chrono::Timestamp{static_cast<std::uint64_t>(frames[0].timestamp_ns)};
        batch.utc_ns = chrono::utc_from_mono_ns(static_cast<std::uint64_t>(frames[0].timestamp_ns),
                                                utc_offset_ns);
    }
    batch.records = records.data();
    batch.record_count = static_cast<std::uint32_t>(count);
    batch.record_capacity = static_cast<std::uint32_t>(count);
    nlohmann::json line;
    to_json(line, batch);
    return line.dump();
}

class HistoryWriter {
public:
    HistoryWriter(std::string path, std::uint64_t max_bytes) noexcept
        : path_(std::move(path)), max_bytes_(max_bytes) {}

    HistoryWriter(const HistoryWriter&) = delete;
    HistoryWriter& operator=(const HistoryWriter&) = delete;

    ~HistoryWriter() { close(); }

    [[nodiscard]] bool open() noexcept {
        fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd_ < 0) {
            return false;
        }
        struct stat status{};
        bytes_ = (::fstat(fd_, &status) == 0) ? static_cast<std::uint64_t>(status.st_size) : 0U;
        return true;
    }

    /// Append one line (the caller supplies the trailing newline). Rotates the
    /// file first when the quota would be exceeded.
    [[nodiscard]] bool append(std::string_view line) noexcept {
        if (fd_ < 0) {
            return false;
        }
        if (max_bytes_ > 0U && bytes_ + line.size() > max_bytes_) {
            rotate();
            if (fd_ < 0) {
                return false;
            }
        }
        std::size_t offset = 0;
        while (offset < line.size()) {
            const ssize_t written = ::write(fd_, line.data() + offset, line.size() - offset);
            if (written > 0) {
                offset += static_cast<std::size_t>(written);
            } else if (errno != EINTR) {
                return false;
            }
        }
        bytes_ += line.size();
        return true;
    }

    void flush() noexcept {
        if (fd_ >= 0) {
            ::fsync(fd_);
        }
    }

    void close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    void rotate() noexcept {
        close();
        const std::string rotated = path_ + ".1";
        static_cast<void>(::unlink(rotated.c_str()));
        static_cast<void>(::rename(path_.c_str(), rotated.c_str()));
        fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        bytes_ = 0U;
    }

    std::string path_;
    std::uint64_t max_bytes_{0};
    std::uint64_t bytes_{0};
    int fd_{-1};
};

} // namespace nexigen::metrics::detail
