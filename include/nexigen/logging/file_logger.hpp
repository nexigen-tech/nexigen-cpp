// nexigen::logging — asynchronous wait-free JSON-lines file logger (design.md
// D8-D15, spec logging/file-logger).
//
// Shape: the producer hot path is the shared detail::Producer (level filter →
// stamp → throttle window → cached tid → staging → try_push — never blocking,
// never allocating, bounded steps). Everything expensive and OS-touching —
// UTC calibration, JSON formatting, escaping, directory rotation, fsync, and
// CPU affinity — lives on one background std::jthread (detail::Async_worker)
// that pins itself first thing (core_mask != 0) and then drains the ring in
// batches into "<base_dir>/<period-label>/<file_name>".
//
// Drop story (spec): over-throttle -> RateLimited + rate-limited counter;
// ring full -> BufferFull + buffer-full counter; on the next window transition
// with pending drops the producer synthesizes a Warning record reporting the
// exact per-cause counts (design D9) — it costs one bounded push per window
// at most, and never re-tries in a loop.
//
// Rotation story (spec "hitch-free"): the consumer alone compares each
// record's period key, mkdirat/openat-switches via pre-allocated path buffers
// (design D11), fsyncs and closes the previous descriptor only after the new
// one opens. Producers keep enqueuing through it all — the pipeline never
// notices anything but an empty/full ring.
//
// CONTRACT: exactly one producer thread per instance (SPSC, design R1); the
// internal worker is the sole consumer. level()/dropped()/pending()/
// writer_*() are third-thread safe — relaxed SNAPSHOTS, diagnostics only.
// flush() is the control-plane barrier (drain + offset re-sample + fsync),
// outside the never-block guarantee that binds the record methods.
//
// DROP SEMANTICS (returned Status of log()/conveniences):
//   {}              record accepted into the ring, OR filtered below the
//                   active level (filtering is normal operation, not a loss)
//   BufferFull      ring full: nothing enqueued, counted in dropped()
//   RateLimited     throttle budget spent for the window, counted in dropped()
//
// Result usage rule: branch on has_value(), never call .value().
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <source_location>
#include <string_view>

#include <cerrno>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/detail/async_logger.hpp>
#include <nexigen/logging/detail/json_writer.hpp>
#include <nexigen/logging/detail/producer.hpp>
#include <nexigen/logging/detail/rotation.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging::detail {

/// Consumer-thread writer: JSON lines to the active rotated descriptor.
class File_sink {
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
        Rotation_kind kind = Rotation_kind::Hourly;
        std::uint64_t custom_ns = 0;
    };

    explicit File_sink(Settings settings) noexcept : settings_{settings} {}

    File_sink(const File_sink&) = delete;
    File_sink& operator=(const File_sink&) = delete;

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
        const core::Status opened = open_current();
        if (opened) {
            fd_ = new_fd_; // promote the staged descriptor to active
        }
        return opened;
    }

    template <class Record>
    void handle(const Record& rec) noexcept {
        const std::int64_t utc =
            chrono::utc_from_mono_ns(rec.stamp.nanoseconds(), utc_offset_ns_);
        if (settings_.kind != Rotation_kind::None) {
            const std::int64_t key = period_key(settings_.kind, settings_.custom_ns, utc);
            if (key != active_key_) {
                last_utc_ns_ = utc;
                const core::Status opened = open_current();
                if (opened) { // spec: never lose the old fd before the new one is live
                    if (fd_ >= 0) {
                        static_cast<void>(::fsync(fd_)); // flush OS-side data of the old period
                        ::close(fd_);
                    }
                    fd_ = new_fd_;
                    active_key_ = key;
                }
            }
        }

        std::size_t len = format_json_line(
            utc, rec.level, std::string_view(settings_.app, settings_.app_len),
            rec.thread_id, std::string_view(rec.file_name), rec.line,
            std::string_view(rec.function_name), rec.message_view(), line_);
        if (len == 0U) {
            ++malformed_; // overflowing/unrepresentable line skipped atomically
            return;       // (never a half-written line in the file)
        }
        if (!write_all(len)) {
            pending_write_error_ = 1U;
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
        const core::Status opened = open_period(
            settings_.kind, settings_.custom_ns, last_utc_ns_,
            std::string_view(settings_.base_dir, settings_.base_len),
            std::string_view(settings_.file_name, settings_.file_name_len), paths_, fd);
        if (!opened) {
            return opened;
        }
        new_fd_ = fd;
        return {};
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
    int fd_ = -1;                        ///< active period descriptor (consumer-only)
    int new_fd_ = -1;                    ///< staged fd during a rotation switch
    std::int64_t active_key_ = 0;        ///< period identity of fd_
    std::int64_t utc_offset_ns_ = 0;     ///< monotonic -> UTC conversion snapshot
    std::int64_t last_utc_ns_ = 0;       ///< utc at the current boundary
    std::uint64_t malformed_ = 0;        ///< formatter overflow skips (diagnostic)
    std::uint8_t pending_write_error_ = 0;
    Rotation_scratch paths_{};           ///< pre-allocated path buffers (D11)
    alignas(64) char line_[json_scratch_capacity]{}; ///< JSON staging scratch (D12)
};

} // namespace nexigen::logging::detail

namespace nexigen::logging {

/// Public vocabulary aliases (policy structs live in detail with the machine).
using Throttle_config = detail::Throttle_config;
using Rotation_schedule = detail::Rotation_kind;

/// Rotation policy: None (single fixed directory), Hourly, Daily, or Custom
/// epoch-anchored windows of custom_ns.
struct Rotation_config {
    Rotation_schedule kind{Rotation_schedule::Hourly};
    std::uint64_t custom_ns{0}; ///< required when kind == Custom
};

/// Construction-time configuration. All views are copied into fixed buffers
/// during initialize(); lengths beyond the buffers are rejected.
struct FileLogger_config {
    std::string_view app_name{"nexigen"};       ///< JSON "app" field (≤ 63 B)
    std::string_view base_dir{};                ///< required, ≤ 511 B
    std::string_view file_name{"app.jsonl"};    ///< ≤ 63 B
    LogLevel initial_level{LogLevel::Info};
    Throttle_config throttle{};
    Rotation_config rotation{};
    std::uint64_t core_mask{0};                 ///< 0 == leave OS placement
};

/// Asynchronous wait-free JSON-lines file logger.
template <std::size_t Capacity = 8192, std::size_t MessageCapacity = 128>
class FileLogger {
    using Producer = detail::Producer<Capacity, MessageCapacity>;

    Producer producer_{};
    detail::Async_worker<Producer, detail::File_sink> worker_{};

public:
    FileLogger() noexcept = default;
    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;
    FileLogger(FileLogger&&) = delete;
    FileLogger& operator=(FileLogger&&) = delete;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    /// Initialization phase (pre real-time): validate config, copy it into
    /// fixed storage, calibrate clocks, start the pinned writer. Invalid
    /// configuration returns InvalidArgument WITHOUT starting the worker.
    [[nodiscard]] core::Status initialize(const FileLogger_config& cfg) noexcept {
        if (cfg.base_dir.empty() || cfg.base_dir.size() > 511 ||
            cfg.base_dir.find('\0') != std::string_view::npos ||
            cfg.file_name.empty() || cfg.file_name.size() > 63 ||
            cfg.file_name.find('\0') != std::string_view::npos ||
            cfg.app_name.size() > 63 ||
            cfg.app_name.find('\0') != std::string_view::npos ||
            (cfg.throttle.enabled &&
             (cfg.throttle.max_records_per_window == 0ULL || cfg.throttle.window_ns == 0ULL)) ||
            (cfg.rotation.kind == Rotation_schedule::Custom && cfg.rotation.custom_ns == 0ULL)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        detail::File_sink::Settings settings{};
        copy_into(cfg.app_name, settings.app, settings.app_len);
        copy_into(cfg.base_dir, settings.base_dir, settings.base_len);
        copy_into(cfg.file_name, settings.file_name, settings.file_name_len);
        settings.kind = cfg.rotation.kind;
        settings.custom_ns = cfg.rotation.custom_ns;

        producer_.set_level(cfg.initial_level);
        producer_.configure_throttle(cfg.throttle);
        const core::Status calibrated = producer_.initialize(); // sleep ~2 ms (cold)
        if (!calibrated) {
            return calibrated;
        }
        return worker_.start(producer_, cfg.core_mask, settings);
    }

    void set_level(LogLevel new_level) noexcept { producer_.set_level(new_level); }
    [[nodiscard]] LogLevel level() const noexcept { return producer_.level(); }

    [[nodiscard]] core::Status log(LogLevel level, std::source_location loc,
                                   std::string_view message) noexcept {
        return producer_.emit(level, loc, message);
    }

    [[nodiscard]] core::Status verbose(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Verbose, loc, msg);
    }
    [[nodiscard]] core::Status debug(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Debug, loc, msg);
    }
    [[nodiscard]] core::Status info(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Info, loc, msg);
    }
    [[nodiscard]] core::Status warning(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Warning, loc, msg);
    }
    [[nodiscard]] core::Status error(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Error, loc, msg);
    }
    [[nodiscard]] core::Status fatal(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Fatal, loc, msg);
    }

    /// Control-plane: drain ring, re-sample the UTC offset, fsync, surface
    /// accumulated write/fsync failures (spec: SystemError).
    [[nodiscard]] core::Status flush() noexcept { return worker_.flush(); }

    /// Drop accounting snapshot (third-thread safe).
    [[nodiscard]] Dropped_stats dropped() const noexcept { return producer_.dropped(); }
    [[nodiscard]] std::size_t pending() const noexcept { return producer_.ring_size(); }

    /// Writer start-phase snapshots (design D15 verification surface):
    /// poll writer_started(), then compare writer_mask() against the
    /// configured core_mask and read the start ErrorCode.
    [[nodiscard]] bool writer_started() const noexcept { return worker_.writer_started(); }
    [[nodiscard]] core::ErrorCode writer_start_code() const noexcept { return worker_.writer_start_code(); }
    [[nodiscard]] std::uint64_t writer_mask() const noexcept { return worker_.writer_mask(); }

private:
    template <std::size_t N>
    static void copy_into(std::string_view src, char (&dst)[N], std::size_t& len_out) noexcept {
        const std::size_t take = src.size() < N ? src.size() : N - 1U;
        if (take > 0) {
            std::memcpy(dst, src.data(), take);
        }
        dst[take] = '\0';
        len_out = take;
    }
};

static_assert(concepts::Logger<FileLogger<>>,
              "FileLogger must satisfy the Logger contract (pinned at header level)");

} // namespace nexigen::logging
