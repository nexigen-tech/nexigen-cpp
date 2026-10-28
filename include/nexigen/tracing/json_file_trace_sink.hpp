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

#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/chrono/time_format.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/detail/json_writer.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// Rotation schedule selector (enum contract of mandates 9–11).
enum class RotationPolicy : std::uint8_t {
    None,   ///< Single file for the sink's lifetime.
    Hourly, ///< A new file every UTC hour.
    Daily,  ///< A new file every UTC day.
};

/// Compile-time canonical name; "Unknown" for garbage casts.
[[nodiscard]] constexpr std::string_view to_string(RotationPolicy policy) noexcept {
    switch (policy) {
        case RotationPolicy::None:   return "None";
        case RotationPolicy::Hourly: return "Hourly";
        case RotationPolicy::Daily:  return "Daily";
        default:                     return "Unknown";
    }
}

/// Parse a canonical policy name; false leaves the out-parameter unchanged.
[[nodiscard]] constexpr bool from_string(std::string_view name, RotationPolicy& out) noexcept {
    if (name == "None")   { out = RotationPolicy::None;   return true; }
    if (name == "Hourly") { out = RotationPolicy::Hourly; return true; }
    if (name == "Daily")  { out = RotationPolicy::Daily;  return true; }
    return false;
}

/// ADL JSON writer/reader (cold path; result.hpp get_ptr pattern).
inline void to_json(nlohmann::json& j, const RotationPolicy policy) {
    j = std::string(to_string(policy));
}
inline void from_json(const nlohmann::json& j, RotationPolicy& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// File sink configuration (value type, full serialization trio; body in
/// src/tracing/sink_format.cpp).
struct JsonFileTraceSink_config {
    std::string path{};               ///< file (None) or filename prefix.
    RotationPolicy rotation{RotationPolicy::None};
    std::uint64_t max_spans_per_window{0}; ///< 0 = unlimited.
    std::uint64_t window_ns{0};            ///< 0 = unlimited.

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const JsonFileTraceSink_config& config);

/// ADL JSON (cold path, full round-trip under JSON_NOEXCEPTION=1); bodies in
/// src/tracing/sink_format.cpp.
void to_json(nlohmann::json& j, const JsonFileTraceSink_config& config);
void from_json(const nlohmann::json& j, JsonFileTraceSink_config& out) noexcept;

/// The JSONL trace sink. All state is consumer-thread plain state (the
/// provider's single worker is the only caller).
class JsonFileTraceSink {
public:
    /// The path/prefix is captured at construction (configuration time);
    /// the file itself opens at start().
    explicit JsonFileTraceSink(const JsonFileTraceSink_config& config) noexcept
        : rotation_(config.rotation),
          max_per_window_(config.max_spans_per_window),
          window_ns_(config.window_ns) {
        copy_path(config.path);
    }

    JsonFileTraceSink(const JsonFileTraceSink&) = delete;
    JsonFileTraceSink& operator=(const JsonFileTraceSink&) = delete;

    /// Move transfers the descriptor and all window state; the source is
    /// left closed (fd = -1) so the destructors never double-close. This is
    /// what makes member-injection construction of composites work.
    JsonFileTraceSink(JsonFileTraceSink&& other) noexcept
        : rotation_(other.rotation_), max_per_window_(other.max_per_window_),
          window_ns_(other.window_ns_), offset_ns_(other.offset_ns_),
          window_start_utc_(other.window_start_utc_), window_key_(other.window_key_),
          tokens_used_(other.tokens_used_),
          dropped_in_window_(other.dropped_in_window_), fd_(other.fd_),
          path_len_(other.path_len_) {
        std::memcpy(path_, other.path_, sizeof(path_));
        other.fd_ = -1;
    }
    JsonFileTraceSink& operator=(JsonFileTraceSink&&) = delete; // stateful sink:
    // rebinding a live sink is a construction action, not an assignment

    /// Open the initial file for the current UTC window. An unopenable path
    /// leaves the sink without a descriptor; on_span then reports
    /// SystemError without writing partial lines.
    [[nodiscard]] core::Status start() noexcept {
        std::int64_t offset_ns = 0;
        if (chrono::sample_utc_offset(offset_ns).has_value()) {
            offset_ns_ = offset_ns;
        }
        return open_for_window(now_utc_ns());
    }

    /// Write one span as one JSON line (rotation + rate limiting first).
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

    /// Push the data to the file (spec: flush surfaces I/O failures).
    [[nodiscard]] core::Status flush() noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (::fsync(fd_) != 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// Close the active file (rotation closes earlier files itself).
    void stop() noexcept {
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
            fd_ = -1;
        }
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;

    /// True when a file is open.
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
        const chrono::Civil_time civil = chrono::civil_from_utc_ns(utc_ns);
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
        const int fd = ::open(name.data(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
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
        detail::Fixed_writer writer{scratch_, detail::json_scratch_capacity};
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
        std::size_t offset = 0;
        while (offset < bytes) {
            const ssize_t written = ::write(fd_, scratch_ + offset, bytes - offset);
            if (written > 0) {
                offset += static_cast<std::size_t>(written);
            } else if (written < 0 && errno == EINTR) {
                continue;
            } else {
                return std::unexpected(core::ErrorCode::SystemError);
            }
        }
        return {};
    }

    RotationPolicy rotation_{RotationPolicy::None};
    std::uint64_t max_per_window_{0};
    std::uint64_t window_ns_{0};
    std::int64_t offset_ns_{0};          ///< sampled UTC offset
    std::int64_t window_start_utc_{INT64_MIN}; ///< rate-limit window anchor
    std::int64_t window_key_{-1};        ///< rotation period identity
    std::uint64_t tokens_used_{0};
    std::uint64_t dropped_in_window_{0};
    int fd_{-1};
    std::size_t path_len_{0};
    char path_[path_capacity + 32]{};   ///< the configured prefix/file path
    char file_[path_capacity + 32]{};   ///< per-window filename build buffer
    char scratch_[detail::json_scratch_capacity]{};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const JsonFileTraceSink& sink);

/// Diagnostic JSON of the live state (cold path); deliberately NO from_json
/// — sinks are constructed from configuration, not parsed from data; body
/// in src/tracing/sink_format.cpp.
void to_json(nlohmann::json& j, const JsonFileTraceSink& sink);

static_assert(concepts::TraceSink<JsonFileTraceSink>,
              "JsonFileTraceSink must satisfy the sink contract");

} // namespace nexigen::tracing

/// The policy enum and the configuration format as their canonical/diagnostic
/// texts (mandate 11).
template <>
struct std::formatter<nexigen::tracing::RotationPolicy> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::tracing::RotationPolicy policy, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::tracing::to_string(policy),
                                                        ctx);
    }
};
template <>
struct std::formatter<nexigen::tracing::JsonFileTraceSink_config>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::JsonFileTraceSink_config& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
template <>
struct std::formatter<nexigen::tracing::JsonFileTraceSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::JsonFileTraceSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
