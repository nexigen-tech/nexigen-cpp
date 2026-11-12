// nexigen::logging — consumer-thread writer: JSON lines to the active rotated
// descriptor (design D8-D15; spec logging/file-logger).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/logging/detail/json_writer.hpp>
#include <nexigen/logging/detail/rotation.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/rotation_schedule.hpp>

namespace nexigen::logging::detail {

/// @brief Consumer-thread writer: JSON lines to the active rotated descriptor.
///
/// Owned by `detail::AsyncWorker` through `std::optional` and constructed in
/// place on `start()`, so all storage is fixed and resident before the first
/// record. Every method runs on the worker thread except construction and the
/// deletion-only copying rule; producers never reach this class.
///
/// @note Spec `logging/file-logger`, requirement "Asynchronous wait-free
///       JSON-lines file output".
class FileSink {
public:
    /// @brief Fixed-footprint snapshot of the validated config.
    ///
    /// Copied at `initialize()` (the logger never holds caller string_views at
    /// runtime) so rotation and formatting touch only inline, record-owned
    /// storage. Lengths are explicit because the arrays are plain C buffers
    /// without a terminator guarantee at capacity.
    struct Settings {
        /// Application name bytes.
        char app[64]{};
        /// Valid bytes in `app`.
        std::size_t app_len = 0;
        /// Configured base directory bytes.
        char base_dir[512]{};
        /// Valid bytes in `base_dir`.
        std::size_t base_len = 0;
        /// Configured file-name bytes.
        char file_name[64]{};
        /// Valid bytes in `file_name`.
        std::size_t file_name_len = 0;
        /// Rotation schedule vocabulary.
        RotationSchedule kind = RotationSchedule::Hourly;
        /// Custom rotation period in nanoseconds (used by `Custom`).
        std::uint64_t custom_ns = 0;
        /// Active-file ceiling in bytes; 0 disables size-based rotation.
        std::uint64_t max_file_size_bytes = 0;
    };

    /// @brief Store a validated settings snapshot.
    ///
    /// @param settings Fixed-footprint config copied into the sink.
    explicit FileSink(Settings settings) noexcept : settings_{settings} {}

    /// @brief Copying is deleted: the sink owns a live file descriptor.
    FileSink(const FileSink&) = delete;
    /// @brief Copy assignment is deleted: the sink owns a live file descriptor.
    FileSink& operator=(const FileSink&) = delete;

    /// @brief Worker-thread cold start: sample UTC and open the current period.
    ///
    /// Samples the UTC offset once, computes the period key containing "now",
    /// opens the matching descriptor, and adopts its current size so a
    /// pre-existing file counts toward the size limit. Failure keeps the worker
    /// running but is reported in the start-code snapshot (design D15
    /// philosophy: pin/open trouble degrades to status surfaces, never to a dead
    /// logger without explanation).
    ///
    /// @return OK once the active descriptor is live; the offset-sampling error,
    ///         `ErrorCode::SystemError` for a clock failure, or the rotation
    ///         open error otherwise.
    /// @note Spec `logging/file-logger`, requirement "Nanosecond UTC timestamps
    ///       via calibrated clocks"; pre-existing content counting toward the
    ///       limit is specified by "Size-based file rotation".
    [[nodiscard]] core::Status start() noexcept {
        std::int64_t offset = 0;
        const core::Status sampled = chrono::sample_utc_offset(offset);
        if (!sampled) {
            return sampled;
        }
        utc_offset_ns_ = offset;

        const core::Result<chrono::Timestamp> now = chrono::Timestamp::now();
        if (!now) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        last_utc_ns_ = static_cast<std::int64_t>(now->nanoseconds()) + offset;
        active_key_ = period_key(settings_.kind, settings_.custom_ns, last_utc_ns_);
        segment_ = 0;
        const core::Status opened = open_current();
        if (opened) {
            fd_ = new_fd_; // promote the staged descriptor to active
            bytes_written_ = file_size(fd_);
        }
        return opened;
    }

    /// @brief Write one drained record as a single JSON line.
    ///
    /// Converts the monotonic stamp to UTC, switches period/segment when the
    /// record crosses a boundary (opening the new descriptor before closing the
    /// old one), renders the deferred format into the sink's scratch, builds the
    /// strict JSON line, and appends it with a retry-on-`EINTR` write loop. A
    /// line that does not fit the scratch is skipped and counted instead of
    /// being half-written; an unwritten line marks a pending error for
    /// `synchronize()`.
    ///
    /// @tparam Record Record type produced by the owning `Producer`.
    /// @param rec Drained record, owned by the worker for the duration of the
    ///        call; `render_into` may set its render-truncation flag.
    /// @note Spec `logging/file-logger`, requirement "Strict JSON record
    ///       schema"; boundary switching is "Hitch-free scheduled directory
    ///       rotation" and "Size-based file rotation"; rendering is
    ///       `logging/log-record`, requirement "Consumer-side rendering of
    ///       packed records".
    template <class Record> void handle(Record& rec) noexcept {
        const std::int64_t utc = chrono::utc_from_mono_ns(rec.stamp.nanoseconds(), utc_offset_ns_);
        if (settings_.kind != RotationSchedule::None) {
            const std::int64_t key = period_key(settings_.kind, settings_.custom_ns, utc);
            if (key != active_key_) {
                last_utc_ns_ = utc;
                segment_ = 0;
                const core::Status opened = open_current();
                if (opened) { // spec: never lose the old fd before the new one is live
                    if (fd_ >= 0) {
                        static_cast<void>(::fsync(fd_)); // flush OS-side data of the old period
                        ::close(fd_);
                    }
                    fd_ = new_fd_;
                    active_key_ = key;
                    bytes_written_ = file_size(fd_);
                }
            }
        }

        // Deferred formatting executes HERE (spec logging/log-record): the
        // pack renders into pre-allocated scratch, allocation-free; the JSON
        // line then escapes the RENDERED text (schema unchanged).
        const std::size_t msg_len = rec.render_into(msg_, msg_capacity);

        std::size_t len = format_json_line(
            utc, rec.level, std::string_view(settings_.app, settings_.app_len), rec.thread_id,
            std::string_view(rec.file_name), rec.line, std::string_view(rec.function_name),
            std::string_view(msg_, msg_len), line_);
        if (len == 0U) {
            ++malformed_; // overflowing/unrepresentable line skipped atomically
            return;       // (never a half-written line in the file)
        }
        if (settings_.max_file_size_bytes != 0ULL && bytes_written_ > 0ULL &&
            bytes_written_ + len > settings_.max_file_size_bytes) {
            rotate_segment(); // open-before-close; on failure the old fd stays live
        }
        if (!write_all(len)) {
            pending_write_error_ = 1U;
        } else {
            bytes_written_ += len;
        }
    }

    /// @brief Flush hook: re-sample UTC, fsync, and surface accumulated failure.
    ///
    /// Re-samples the UTC offset (NTP tolerance, design D10) so wall-clock
    /// corrections reach subsequent records, fsyncs the active descriptor, and
    /// reports any write/fsync failure accumulated since the previous call.
    ///
    /// @return OK when no pending write failure and the fsync succeeded;
    ///         `ErrorCode::SystemError` otherwise.
    /// @note Spec `logging/file-logger`, requirement "Flush synchronizes the
    ///       file" (including the "Flush surfaces write failure" scenario).
    [[nodiscard]] core::Status synchronize() noexcept {
        std::int64_t offset = 0;
        if (chrono::sample_utc_offset(offset)) {
            utc_offset_ns_ = offset;
        }
        bool failed = pending_write_error_ != 0U;
        pending_write_error_ = 0U;
        if (fd_ >= 0 && ::fsync(fd_) != 0) {
            failed = true;
        }
        if (failed) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// @brief Worker shutdown: fsync and close the active descriptor.
    ///
    /// Idempotent: the descriptor is reset to `-1`, so a second call is a no-op.
    /// Called by the worker after its final drain and synchronize.
    ///
    /// @note Spec `logging/file-logger`, requirement "Flush synchronizes the
    ///       file"; the worker calls this after its final drain and publish.
    void stop() noexcept {
        if (fd_ >= 0) {
            static_cast<void>(::fsync(fd_));
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    /// @brief Open the descriptor for the current period and segment.
    ///
    /// Renders the period directory plus segment name into `paths_` and stages
    /// the descriptor in `new_fd_`. The active `fd_` is untouched, so a failed
    /// open leaves the previous file live.
    ///
    /// @return OK when a descriptor was staged; the rotation error otherwise.
    /// @note Spec `logging/file-logger`, requirement "Hitch-free scheduled
    ///       directory rotation" (open before close).
    [[nodiscard]] core::Status open_current() noexcept {
        int fd = -1;
        const core::Status opened = open_segment(
            settings_.kind, settings_.custom_ns, last_utc_ns_,
            std::string_view(settings_.base_dir, settings_.base_len),
            std::string_view(settings_.file_name, settings_.file_name_len), segment_, paths_, fd);
        if (!opened) {
            return opened;
        }
        new_fd_ = fd;
        return {};
    }

    /// @brief Size-based segment switch with open-before-close ordering.
    ///
    /// Opens the next numbered segment BEFORE closing the current one (same
    /// never-lose-a-line order as directory rotation). On failure the previous
    /// descriptor stays live and `segment_` rolls back, so the next size check
    /// retries instead of writing to a dead descriptor.
    ///
    /// @note Spec `logging/file-logger`, requirement "Size-based file rotation".
    void rotate_segment() noexcept {
        ++segment_;
        const core::Status opened = open_current();
        if (!opened) {
            --segment_;
            return;
        }
        if (fd_ >= 0) {
            static_cast<void>(::fsync(fd_));
            ::close(fd_);
        }
        fd_ = new_fd_;
        bytes_written_ = file_size(fd_);
    }

    /// @brief Read the current size of an open descriptor.
    ///
    /// @param fd Descriptor to stat; a negative descriptor yields 0.
    /// @return Size in bytes, or 0 when the stat fails or reports a negative
    ///         size.
    /// @note Spec `logging/file-logger`, requirement "Size-based file rotation"
    ///       (pre-existing content counts toward the limit).
    [[nodiscard]] static std::uint64_t file_size(int fd) noexcept {
        struct stat st{};
        if (fd < 0 || ::fstat(fd, &st) != 0 || st.st_size < 0) {
            return 0;
        }
        return static_cast<std::uint64_t>(st.st_size);
    }

    /// @brief Append the staged line to the active descriptor in full.
    ///
    /// Retries on `EINTR` and keeps writing until every byte is committed or a
    /// real error occurs; never allocates and never blocks on anything but the
    /// file itself.
    ///
    /// @param bytes Number of valid bytes staged in `line_`.
    /// @return `true` when all bytes were written, `false` on a write error.
    /// @note Spec `logging/file-logger`, requirement "Strict JSON record schema"
    ///       (one complete line per write attempt).
    bool write_all(std::size_t bytes) noexcept {
        std::size_t off = 0;
        while (off < bytes) {
            const ssize_t wrote = ::write(fd_, line_ + off, bytes - off);
            if (wrote > 0) {
                off += static_cast<std::size_t>(wrote);
            } else if (errno == EINTR) {
                continue;
            } else {
                return false;
            }
        }
        return true;
    }

    /// Validated fixed-footprint config supplied at construction.
    Settings settings_{};
    int fd_ = -1;                     ///< active period descriptor (consumer-only)
    int new_fd_ = -1;                 ///< staged fd during a rotation switch
    std::int64_t active_key_ = 0;     ///< period identity of fd_
    std::int64_t utc_offset_ns_ = 0;  ///< monotonic -> UTC conversion snapshot
    std::int64_t last_utc_ns_ = 0;    ///< utc at the current boundary
    std::uint64_t segment_ = 0;       ///< size-based segment index (writer-only)
    std::uint64_t bytes_written_ = 0; ///< active segment size (writer-only)
    std::uint64_t malformed_ = 0;     ///< formatter overflow skips (diagnostic)
    /// Set after a failed write and cleared by the next `synchronize()`.
    std::uint8_t pending_write_error_ = 0;
    RotationScratch paths_{}; ///< pre-allocated path buffers (D11)
    /// Render scratch capacity reserved for one deferred message.
    static constexpr std::size_t msg_capacity = 512;
    char msg_[msg_capacity]{};                       ///< pre-allocated render scratch (design D7)
    alignas(64) char line_[json_scratch_capacity]{}; ///< JSON staging scratch (D12)
};

} // namespace nexigen::logging::detail
