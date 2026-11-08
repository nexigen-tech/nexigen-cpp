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
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/rotation_schedule.hpp>

namespace nexigen::logging::detail {

/// Consumer-thread writer: JSON lines to the active rotated descriptor.
class FileSink {
public:
    /// Fixed-footprint snapshot of the validated config (copied at
    /// initialize() — the logger never holds caller string_views at runtime).
    struct Settings {
        char app[64]{};
        std::size_t app_len = 0;
        char base_dir[512]{};
        std::size_t base_len = 0;
        char file_name[64]{};
        std::size_t file_name_len = 0;
        RotationSchedule kind = RotationSchedule::Hourly;
        std::uint64_t custom_ns = 0;
        std::uint64_t max_file_size_bytes = 0;
    };

    explicit FileSink(Settings settings) noexcept : settings_{settings} {}

    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;

    /// Worker-thread cold start: UTC offset sample + open the period that
    /// covers "now". Failure keeps the worker running but is reported in the
    /// start-code snapshot (design D15 philosophy: pin/open trouble degrades
    /// to status surfaces, never to a dead logger without explanation).
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

    /// flush() hook: re-sample the UTC offset (NTP tolerance, D10), fsync the
    /// active descriptor, and surface any accumulated write/fsync failure.
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

    void stop() noexcept {
        if (fd_ >= 0) {
            static_cast<void>(::fsync(fd_));
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
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

    /// Size-based segment switch: open the next numbered file BEFORE closing
    /// the current one (same never-lose-a-line order as directory rotation).
    /// On failure the previous descriptor stays live and `segment_` rolls back,
    /// so the next size check retries instead of writing to a dead fd.
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

    [[nodiscard]] static std::uint64_t file_size(int fd) noexcept {
        struct stat st{};
        if (fd < 0 || ::fstat(fd, &st) != 0 || st.st_size < 0) {
            return 0;
        }
        return static_cast<std::uint64_t>(st.st_size);
    }

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

    Settings settings_{};
    int fd_ = -1;                     ///< active period descriptor (consumer-only)
    int new_fd_ = -1;                 ///< staged fd during a rotation switch
    std::int64_t active_key_ = 0;     ///< period identity of fd_
    std::int64_t utc_offset_ns_ = 0;  ///< monotonic -> UTC conversion snapshot
    std::int64_t last_utc_ns_ = 0;    ///< utc at the current boundary
    std::uint64_t segment_ = 0;       ///< size-based segment index (writer-only)
    std::uint64_t bytes_written_ = 0; ///< active segment size (writer-only)
    std::uint64_t malformed_ = 0;     ///< formatter overflow skips (diagnostic)
    std::uint8_t pending_write_error_ = 0;
    RotationScratch paths_{}; ///< pre-allocated path buffers (D11)
    static constexpr std::size_t msg_capacity = 512;
    char msg_[msg_capacity]{};                       ///< pre-allocated render scratch (design D7)
    alignas(64) char line_[json_scratch_capacity]{}; ///< JSON staging scratch (D12)
};

} // namespace nexigen::logging::detail
