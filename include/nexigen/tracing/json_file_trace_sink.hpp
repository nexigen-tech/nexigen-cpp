// nexigen::tracing — rotating, rate-limited JSON-lines trace sink (design
// D11; spec tracing/trace-sinks "JsonFileTraceSink JSONL output" +
// "rotation and rate limiting").
//
// One complete JSON object per span per line (JSONL), UTC ISO-8601 stamps
// through nexigen::chrono, identifiers as zero-padded 16-char hex, kind/
// status canonical, attributes typed and escaped. flush() = fsync.
//
// ROTATION: hourly/daily windows are derived from each incoming span's UTC
// time (no timers, no polling — the logging rotation pattern); on a window
// change the sink closes the current file and opens
//   Hourly: <prefix>.<YYYYMMDDTHH>   Daily: <prefix>.<YYYYMMDD>
// with None keeping the single configured file for the sink's lifetime.
// Filenames render into fixed buffers; open failures leave the PREVIOUS
// descriptor live (stale-but-valid output, never lost records silently).
//
// RATE LIMITING: a configurable budget of spans per monotonic-length UTC
// window; excess spans are counted and NOT written, and a drop-summary JSON
// line with the exact count is emitted at the window boundary BEFORE any
// further record lands (a budget of 0/window 0 = unlimited: everything is
// written and no summaries appear).
//
// The UTC offset is sampled once per window transition on the consumer
// thread (cold); a failed sample falls back to the previous offset.
//
// COLD PATH ONLY: this sink runs on the provider's background consumer.
// The write path allocates nothing (fixed scratch + bounds-checked writer);
// std::string exists only in the CONFIGURATION (setup-time).
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/chrono/civil_time.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/sink_io.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/detail/json_writer.hpp>
#include <nexigen/tracing/json_file_trace_sink_config.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// @brief Rotating, rate-limited JSON-lines sink (one JSON object per span).
///
/// Every `on_span` advances the UTC window, optionally rotates the file
/// (`Hourly`/`Daily` names the next file from the span's own UTC time, `None`
/// keeps one file), applies the per-window span budget, then appends one
/// complete escaped JSON line with UTC ISO-8601 timestamps, hexadecimal ids,
/// canonical kind/status, and typed attributes. `flush()` is an `fsync` so I/O
/// failures surface as `SystemError`.
///
/// Rotation and rate limiting are driven by incoming spans only: no timer and
/// no polling runs. Over-budget spans are counted and not written, and the
/// exact drop count is emitted as a summary line at the next window boundary
/// before any newer record. All state, including the UTC offset snapshot and
/// window anchor, is consumer-thread plain state (the provider's single
/// background worker is the only caller). The write path uses fixed scratch
/// and allocates nothing; `std::string` exists only in the configuration.
///
/// @note Spec `tracing/trace-sinks`, requirements "JsonFileTraceSink JSONL
///       output" and "JsonFileTraceSink rotation and rate limiting".
class JsonFileTraceSink {
public:
    /// @brief Capture the configuration; the file itself opens at `start()`.
    ///
    /// Copies the path into fixed storage (truncating only if it exceeds the
    /// buffer) and the application name into its 63-byte field.
    ///
    /// @param config Path/prefix, application name, rotation policy, and rate
    ///        limits; the sink snapshots them, so the source may be released.
    /// @note Spec `tracing/trace-sinks`, requirement "Sink configuration
    ///       serialization".
    explicit JsonFileTraceSink(const JsonFileTraceSinkConfig& config) noexcept
        : rotation_(config.rotation), max_per_window_(config.max_spans_per_window),
          window_ns_(config.window_ns) {
        copy_path(config.path);
        const std::size_t take = config.app_name.size() < 63 ? config.app_name.size() : 63;
        for (std::size_t i = 0; i < take; ++i) {
            app_[i] = config.app_name[i];
        }
        app_len_ = take;
    }

    /// @brief Copy construction is deleted: the sink owns a descriptor.
    JsonFileTraceSink(const JsonFileTraceSink&) = delete;
    /// @brief Copy assignment is deleted: the sink owns a descriptor.
    JsonFileTraceSink& operator=(const JsonFileTraceSink&) = delete;

    /// @brief Move the descriptor and all rotation/rate-limit window state.
    ///
    /// This is what makes member-injection construction of composites work.
    ///
    /// @param other Source sink; left closed (`fd_ == -1`) so neither
    ///        destructor can double-close.
    /// @warning A moved-from sink must not be used for delivery.
    JsonFileTraceSink(JsonFileTraceSink&& other) noexcept
        : rotation_(other.rotation_), max_per_window_(other.max_per_window_),
          window_ns_(other.window_ns_), offset_ns_(other.offset_ns_),
          window_start_utc_(other.window_start_utc_), window_key_(other.window_key_),
          tokens_used_(other.tokens_used_), dropped_in_window_(other.dropped_in_window_),
          discarded_lines_(other.discarded_lines_), fd_(other.fd_), path_len_(other.path_len_),
          app_len_(other.app_len_) {
        std::memcpy(path_, other.path_, sizeof(path_));
        std::memcpy(app_, other.app_, sizeof(app_));
        other.fd_ = -1;
    }
    /// @brief Move assignment is deleted: rebinding a live sink is a
    ///        construction action, not an assignment.
    JsonFileTraceSink& operator=(JsonFileTraceSink&&) = delete;

    /// @brief Open the initial file for the current UTC window.
    ///
    /// Samples the UTC offset first, then opens the configured file/path with
    /// append semantics.
    ///
    /// @return Success when the file opened; `SystemError` when the path is
    ///         unopenable, which leaves the sink without a descriptor and makes
    ///         later `on_span` calls report `SystemError` without partial lines.
    /// @note Spec `tracing/trace-sinks`, requirement "JsonFileTraceSink JSONL
    ///       output".
    [[nodiscard]] core::Status start() noexcept {
        std::int64_t offset_ns = 0;
        if (chrono::sample_utc_offset(offset_ns).has_value()) {
            offset_ns_ = offset_ns;
        }
        return open_for_window(now_utc_ns());
    }

    /// @brief Write one span as one complete JSON line.
    ///
    /// Rotation and rate limiting run first: a window change closes and opens
    /// the next file and resets the token budget, and a span beyond the budget
    /// is counted but not written. At a rate-limit window boundary the pending
    /// exact drop summary is written before the new window's first record.
    ///
    /// @param record Span to render.
    /// @return Success when the line was written (or the span was deliberately
    ///         dropped by the budget); `SystemError` when rotation or the write
    ///         fails, or `BufferFull` when the line exceeds the fixed scratch
    ///         (the line is discarded whole and counted).
    /// @note Spec `tracing/trace-sinks`, requirements "JsonFileTraceSink JSONL
    ///       output" (scratch overflow discards and counts) and
    ///       "JsonFileTraceSink rotation and rate limiting". The delivery path
    ///       allocates nothing.
    [[nodiscard]] core::Status on_span(const SpanRecord& record) noexcept {
        const std::int64_t utc_ns =
            chrono::utc_from_mono_ns(record.start.nanoseconds(), offset_ns_);
        const core::Status rotated = advance_window(utc_ns);
        if (!rotated) {
            return rotated; // rotation failure: no partial line emitted
        }
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (max_per_window_ != 0ULL && window_ns_ != 0ULL) {
            if (window_start_utc_ == INT64_MIN) {
                window_start_utc_ = utc_ns; // first record opens window 0
            } else if (utc_ns - window_start_utc_ >= static_cast<std::int64_t>(window_ns_)) {
                // Window boundary: the exact drop summary BEFORE further
                // records land (spec "Rate limiting drops with an exact
                // summary").
                if (dropped_in_window_ != 0ULL) {
                    const core::Status summary = write_drop_summary();
                    if (!summary) {
                        return summary;
                    }
                }
                window_start_utc_ += static_cast<std::int64_t>(
                    (static_cast<std::uint64_t>(utc_ns - window_start_utc_) / window_ns_) *
                    window_ns_);
                tokens_used_ = 0;
                dropped_in_window_ = 0;
            }
            if (tokens_used_ >= max_per_window_) {
                ++dropped_in_window_; // not written, exactly counted
                return {};
            }
            ++tokens_used_;
        }
        return write_record_line(record, utc_ns);
    }

    /// @brief Synchronize the active file with `fsync`.
    ///
    /// @return Success when the descriptor is open and `fsync` succeeds;
    ///         `SystemError` when no file is open or synchronization fails.
    /// @note Spec `tracing/trace-sinks`, requirement "JsonFileTraceSink JSONL
    ///       output" (flush surfaces I/O failures).
    [[nodiscard]] core::Status flush() noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (::fsync(fd_) != 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// @brief Close the active file.
    ///
    /// Rotation already closed earlier files; an already-closed sink is a
    /// no-op. Idempotent.
    /// @note Spec `tracing/trace-sinks`, requirement "JsonFileTraceSink JSONL
    ///       output".
    void stop() noexcept {
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
            fd_ = -1;
        }
    }

    /// @brief Render the sink state as a cold-path diagnostic string.
    ///
    /// @return Text naming the path/prefix, rotation, rate-limit budget, active
    ///         window, and discarded-line counter; allocates.
    /// @note Project mandate 9 (`to_string`); the discarded-line counter makes
    ///       scratch-overflow discards visible as required by the spec.
    [[nodiscard]] std::string to_string() const;

    /// @brief Report whether a file is currently open.
    ///
    /// @return `true` while the sink holds a valid descriptor.
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

private:
    static constexpr std::size_t path_capacity = 512; ///< prefix + '.' + label

    void copy_path(const std::string& path) noexcept {
        const std::size_t take = path.size() < path_capacity - 24U
                                     ? path.size()
                                     : path_capacity - 24U; // room for the label
        for (std::size_t i = 0; i < take; ++i) {
            path_[i] = path[i];
        }
        path_[take] = '\0';
        path_len_ = take;
    }

    /// The consumer thread's UTC now (offset snapshot; stale-but-valid on a
    /// failed re-sample).
    [[nodiscard]] std::int64_t now_utc_ns() const noexcept {
        const core::Result<chrono::Timestamp> now = chrono::Timestamp::now();
        std::uint64_t mono = 0;
        if (now) {
            mono = now->nanoseconds();
        }
        return chrono::utc_from_mono_ns(mono, offset_ns_);
    }

    /// Period identity of a UTC instant (monotonic in utc_ns): hourly =
    /// day*24+hour, daily = day. None always returns 0 (one window).
    [[nodiscard]] static std::int64_t period_key(RotationPolicy policy,
                                                 std::int64_t utc_ns) noexcept {
        if (policy == RotationPolicy::None) {
            return 0;
        }
        const chrono::CivilTime civil = chrono::civil_from_utc_ns(utc_ns);
        const std::int64_t days = chrono::days_from_utc_ns(utc_ns);
        if (policy == RotationPolicy::Hourly) {
            return days * 24 + civil.hour;
        }
        return days;
    }

    /// Render "<prefix>.<label>" for the window containing utc_ns; hourly
    /// labels YYYYMMDDTHH, daily labels YYYYMMDD. Returns the filename view
    /// into path_scratch_ (empty view when it does not fit).
    [[nodiscard]] std::string_view window_filename(std::int64_t utc_ns) noexcept;

    /// Open the file for the window containing utc_ns (closes nothing: the
    /// caller swaps descriptors only on success).
    [[nodiscard]] core::Status open_for_window(std::int64_t utc_ns) noexcept {
        std::string_view name;
        if (rotation_ == RotationPolicy::None) {
            name = std::string_view(path_, path_len_);
        } else {
            name = window_filename(utc_ns);
            if (name.empty()) {
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
        // One cold open at rotation frequency; open_output copies the path
        // (no reliance on the in-place trailing NUL any more).
        const int fd = core::sink_io::open_output(name, /*append=*/true);
        if (fd < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_)); // success swap: old file closed now
        }
        fd_ = fd;
        window_key_ = period_key(rotation_, utc_ns);
        return {};
    }

    /// Rotation + (for the rate limit) nothing else: detect the UTC window
    /// change of an incoming span; on change re-sample the offset (fallback:
    /// previous), close+open the next file, and reset the token budget.
    [[nodiscard]] core::Status advance_window(std::int64_t utc_ns) noexcept {
        if (rotation_ == RotationPolicy::None) {
            return {};
        }
        const std::int64_t key = period_key(rotation_, utc_ns);
        if (fd_ < 0 || key != window_key_) {
            std::int64_t offset_ns = 0;
            if (chrono::sample_utc_offset(offset_ns).has_value()) {
                offset_ns_ = offset_ns; // per-window re-sample (cold)
            }
            const core::Status opened = open_for_window(utc_ns);
            if (!opened) {
                return opened;
            }
        }
        return {};
    }

    /// The drop-summary line (exact count, valid JSON).
    [[nodiscard]] core::Status write_drop_summary() noexcept {
        detail::FixedWriter writer{scratch_, detail::json_scratch_capacity};
        static_cast<void>(writer.append_sv("{\"drop_summary\":{\"dropped\":"));
        static_cast<void>(writer.append_u64(dropped_in_window_));
        static_cast<void>(writer.append_sv("}}\n"));
        if (!writer.ok()) {
            return std::unexpected(core::ErrorCode::BufferFull);
        }
        return write_all(writer.size());
    }

    /// One complete JSON object for one span.
    [[nodiscard]] core::Status write_record_line(const SpanRecord& record,
                                                 std::int64_t utc_ns) noexcept;

    [[nodiscard]] core::Status write_all(std::size_t bytes) noexcept {
        return core::sink_io::write_all(fd_, scratch_, bytes);
    }

    RotationPolicy rotation_{RotationPolicy::None};
    std::uint64_t max_per_window_{0};
    std::uint64_t window_ns_{0};
    std::int64_t offset_ns_{0};                ///< sampled UTC offset
    std::int64_t window_start_utc_{INT64_MIN}; ///< rate-limit window anchor
    std::int64_t window_key_{-1};              ///< rotation period identity
    std::uint64_t tokens_used_{0};
    std::uint64_t dropped_in_window_{0};
    std::uint64_t discarded_lines_{0}; ///< lines over the JSON scratch (P2-18)
    int fd_{-1};
    std::size_t path_len_{0};
    std::size_t app_len_{0};
    char path_[path_capacity + 32]{}; ///< the configured prefix/file path
    char app_[64]{};                  ///< the configured application name
    char file_[path_capacity + 32]{}; ///< per-window filename build buffer
    char scratch_[detail::json_scratch_capacity]{};
};

/// @brief Free diagnostic helper mirroring `JsonFileTraceSink::to_string()`.
///
/// @param sink Sink to describe.
/// @return The same text the member returns.
/// @note Project mandate 9 (`to_string`); cold path only.
[[nodiscard]] std::string to_string(const JsonFileTraceSink& sink);

/// @brief Serialize the live sink's configuration and state as JSON.
///
/// Deliberately no `from_json`: sinks are constructed from configuration, not
/// parsed from data (spec "Live sink serialization surfaces").
///
/// @param j Destination JSON value, overwritten with the sink state.
/// @param sink Sink to describe.
/// @note Spec `tracing/trace-sinks`, requirement "Live sink serialization
///       surfaces"; project mandate 10 (JSON).
void to_json(nlohmann::json& j, const JsonFileTraceSink& sink);

static_assert(concepts::TraceSink<JsonFileTraceSink>,
              "JsonFileTraceSink must satisfy the sink contract");

} // namespace nexigen::tracing

/// @brief `std::formatter` for `JsonFileTraceSink`.
///
/// @note Project mandate 11 (`std::formatter`); formatting is cold path only.
template <>
struct std::formatter<nexigen::tracing::JsonFileTraceSink> : std::formatter<std::string_view> {
    /// @brief Format the sink through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param sink Sink to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::JsonFileTraceSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
