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

/// @brief Serialize one window of frames as a `MetricBatchSnapshot` JSON object.
///
/// The returned line carries `stamp_ns`, `utc_ns` (the window stamp converted
/// with `utc_offset_ns`), `record_count` and the `records` array, matching the
/// existing snapshot serialization. It has no trailing newline; the caller
/// appends that. Cold path: allocates through `nlohmann::json` and `std::vector`.
///
/// @param frames Contiguous frames of one flushed window.
/// @param count Number of frames to serialize; `0` yields an empty record array.
/// @param utc_offset_ns Local offset added to the monotonic window stamp.
/// @return The serialized JSON line, without a trailing newline.
/// @note Spec `metrics/metrics-daemon`, requirement "Daemon writes a
///       quota-bounded JSON-lines history"; project mandate 10 (JSON).
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

/// @brief Append-only JSON-lines history file with quota-driven rotation.
///
/// Writes one line per flushed window and rotates the active file to
/// `<path>.1` once appending would exceed the byte quota, bounding storage to
/// roughly twice the quota. Owns its descriptor; single-writer (the drain
/// thread). Cold path only: opening, writing, `fsync` and rotation happen once
/// per flush interval, never per sample.
///
/// @note Spec `metrics/metrics-daemon`, requirement "Daemon writes a
///       quota-bounded JSON-lines history".
class HistoryWriter {
public:
    /// @brief Create the writer; no file is touched until `open()`.
    ///
    /// @param path File to append to.
    /// @param max_bytes Rotate before exceeding this size; `0` disables the quota.
    HistoryWriter(std::string path, std::uint64_t max_bytes) noexcept
        : path_(std::move(path)), max_bytes_(max_bytes) {}

    /// @brief Copy construction is deleted: the writer owns a descriptor.
    HistoryWriter(const HistoryWriter&) = delete;
    /// @brief Copy assignment is deleted: the writer owns a descriptor.
    HistoryWriter& operator=(const HistoryWriter&) = delete;

    /// @brief Close the descriptor if it is still open.
    ~HistoryWriter() { close(); }

    /// @brief Open the history file for append and learn its current size.
    ///
    /// @return `true` when the file was opened; `false` when `open` fails, in
    ///         which case `append` also fails until a later successful `open`.
    [[nodiscard]] bool open() noexcept {
        fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd_ < 0) {
            return false;
        }
        struct stat status{};
        bytes_ = (::fstat(fd_, &status) == 0) ? static_cast<std::uint64_t>(status.st_size) : 0U;
        return true;
    }

    /// @brief Append one line, rotating the file first when the quota is exceeded.
    ///
    /// @param line Bytes to append; the caller supplies the trailing newline.
    /// @return `true` when every byte was written; `false` when the file is not
    ///         open or a write fails with an error other than `EINTR`.
    /// @note Spec `metrics/metrics-daemon`, requirement "Daemon writes a
    ///       quota-bounded JSON-lines history".
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

    /// @brief Flush the descriptor to stable storage with `fsync`.
    ///
    /// A no-op when the file is not open; the `fsync` result is ignored because
    /// this is called on the shutdown path.
    /// @note Spec `metrics/metrics-daemon`, requirement "Daemon writes a
    ///       quota-bounded JSON-lines history".
    void flush() noexcept {
        if (fd_ >= 0) {
            ::fsync(fd_);
        }
    }

    /// @brief Close the descriptor and mark the writer closed.
    ///
    /// Idempotent; a later `append` fails until `open()` is called again.
    void close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    /// @brief Close, move the active file to `<path>.1`, and reopen a fresh file.
    ///
    /// Replacing `<path>.1` bounds storage to about twice the quota; a failed
    /// reopen leaves the descriptor at `-1`, which `append` reports.
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
