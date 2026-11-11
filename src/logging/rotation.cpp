// nexigen::logging — rotation implementation (design.md D11). See
// detail/rotation.hpp for semantics. Consumer-thread only; the buffers come
// from the caller's pre-allocated scratch, and the path building never
// touches the heap.
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/chrono/civil_time.hpp>
#include <nexigen/logging/detail/rotation.hpp>

namespace nexigen::logging::detail {

using namespace std::literals::string_view_literals;

namespace {

/// Append src into buf at *pos; false on overflow (nothing written).
[[nodiscard]] bool put(std::string_view src, char (&buf)[560], std::size_t& pos) noexcept {
    if (src.size() > sizeof buf - pos - 1) {
        return false;
    }
    std::memcpy(buf + pos, src.data(), src.size());
    pos += src.size();
    buf[pos] = '\0';
    return true;
}

/// Render the Custom-period label "epoch-<idx>" into buf; returns length or 0.
std::size_t put_epoch_label(std::int64_t value, char (&buf)[64]) noexcept {
    std::size_t out = 0;
    const bool neg = value < 0;
    std::uint64_t v =
        neg ? static_cast<std::uint64_t>(-(value + 1)) + 1ULL : static_cast<std::uint64_t>(value);
    char digits[24];
    std::size_t n = 0;
    do {
        digits[n++] = static_cast<char>('0' + (v % 10ULL));
        v /= 10ULL;
    } while (v != 0ULL);
    const std::size_t need = n + (neg ? 1U : 0U) + 6U; // + "epoch-"
    if (need > sizeof buf) {
        return 0;
    }
    for (std::string_view p = "epoch-"sv; const char c : p) {
        buf[out++] = c;
    }
    if (neg) {
        buf[out++] = '-';
    }
    while (n > 0) {
        buf[out++] = digits[--n];
    }
    buf[out] = '\0';
    return out;
}

/// Render `file_name` with an optional ".<segment>" suffix before its last
/// extension into `out` (max 255 bytes by config validation). Returns the
/// resulting length, or 0 when it would not fit (never truncates).
[[nodiscard]] std::size_t segment_name(std::string_view file_name, std::uint64_t segment,
                                       char (&out)[288]) noexcept {
    if (segment == 0U) {
        if (file_name.size() > sizeof out) {
            return 0;
        }
        std::memcpy(out, file_name.data(), file_name.size());
        out[file_name.size()] = '\0';
        return file_name.size();
    }
    char digits[24];
    const auto converted = std::to_chars(digits, digits + sizeof digits, segment);
    if (converted.ec != std::errc{}) {
        return 0;
    }
    const std::size_t digit_len = static_cast<std::size_t>(converted.ptr - digits);
    const std::size_t dot = file_name.rfind('.');
    const bool has_extension = dot != std::string_view::npos && dot != 0U;
    const std::size_t stem_len = has_extension ? dot : file_name.size();
    const std::size_t ext_len = has_extension ? file_name.size() - dot : 0U;
    const std::size_t need = stem_len + 1U + digit_len + ext_len;
    if (need >= sizeof out) {
        return 0;
    }
    std::size_t pos = 0;
    std::memcpy(out + pos, file_name.data(), stem_len);
    pos += stem_len;
    out[pos++] = '.';
    std::memcpy(out + pos, digits, digit_len);
    pos += digit_len;
    if (has_extension) {
        std::memcpy(out + pos, file_name.data() + dot, ext_len);
        pos += ext_len;
    }
    out[pos] = '\0';
    return pos;
}

/// Validate the caller-supplied path inputs once (never NUL-terminated here).
[[nodiscard]] bool validate_segment_request(std::string_view base_dir,
                                            std::string_view file_name) noexcept {
    return !base_dir.empty() && !file_name.empty() && base_dir.size() <= 511U &&
           file_name.size() <= 255U && base_dir.find('\0') == std::string_view::npos &&
           file_name.find('\0') == std::string_view::npos;
}

/// mkdirat with the steady-state EEXIST tolerance; other errno -> SystemError.
[[nodiscard]] core::Status ensure_directory(const char* path) noexcept {
    if (::mkdirat(AT_FDCWD, path, 0755) != 0 && errno != EEXIST) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

/// Stage the base directory into scratch.file and create it if missing.
[[nodiscard]] core::Status prepare_base_directory(std::string_view base_dir,
                                                  RotationScratch& scratch) noexcept {
    std::size_t bpos = 0;
    if (!put(base_dir, scratch.file, bpos)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return ensure_directory(scratch.file);
}

/// Render "<base_dir>[/<label>]" into scratch.dir (label only when rotating).
[[nodiscard]] core::Status build_rotation_dir(RotationSchedule kind, std::uint64_t custom_ns,
                                              std::int64_t utc_ns, std::string_view base_dir,
                                              RotationScratch& scratch) noexcept {
    std::size_t pos = 0;
    if (!put(base_dir, scratch.dir, pos)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (kind == RotationSchedule::None) {
        return {};
    }
    char label[64];
    const std::size_t label_len = period_label(kind, custom_ns, utc_ns, label);
    if (label_len == 0U || !put("/"sv, scratch.dir, pos) ||
        !put(std::string_view(label, label_len), scratch.dir, pos)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

/// Render "<dir>/<effective_name>" into scratch.file.
[[nodiscard]] bool build_segment_file(RotationScratch& scratch, const char (&effective_name)[288],
                                      std::size_t effective_len) noexcept {
    std::size_t fpos = 0;
    return put(std::string_view(scratch.dir), scratch.file, fpos) &&
           put("/"sv, scratch.file, fpos) &&
           put(std::string_view(effective_name, effective_len), scratch.file, fpos);
}

/// Open the staged path for append, creating it if absent.
[[nodiscard]] core::Status open_append(const char* path, int& fd_out) noexcept {
    const int fd = ::openat(AT_FDCWD, path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    fd_out = fd;
    return {};
}

} // namespace

std::int64_t period_key(RotationSchedule kind, std::uint64_t custom_ns,
                        std::int64_t utc_ns) noexcept {
    switch (kind) {
        case RotationSchedule::Hourly: {
            // Same civil decomposition the labels use, so key changes and
            // visible boundaries cannot disagree.
            const chrono::CivilTime ct = chrono::civil_from_utc_ns(utc_ns);
            return chrono::days_from_utc_ns(utc_ns) * 24 + static_cast<std::int64_t>(ct.hour);
        }
        case RotationSchedule::Daily:  return chrono::days_from_utc_ns(utc_ns);
        case RotationSchedule::Custom: {
            if (custom_ns == 0ULL) {
                return 0; // config error is surfaced at initialize(); stay monotone
            }
            const std::uint64_t u = static_cast<std::uint64_t>(utc_ns); // UTC >= epoch in practice
            return static_cast<std::int64_t>(u / custom_ns);
        }
        case RotationSchedule::None:
        default:                     return 0;
    }
}

std::size_t period_label(RotationSchedule kind, std::uint64_t custom_ns, std::int64_t utc_ns,
                         char (&buf)[64]) noexcept {
    buf[0] = '\0';
    switch (kind) {
        case RotationSchedule::Hourly:
        case RotationSchedule::Daily:  {
            char iso[31];
            chrono::format_utc_iso8601_ns(utc_ns, iso);
            // Hourly: "YYYY-MM-DDTHH" (13 chars); Daily: "YYYY-MM-DD" (10).
            const std::size_t len = (kind == RotationSchedule::Hourly) ? 13U : 10U;
            std::memcpy(buf, iso, len);
            buf[len] = '\0';
            return len;
        }
        case RotationSchedule::Custom:
            return put_epoch_label(period_key(kind, custom_ns, utc_ns), buf);
        case RotationSchedule::None:
        default:                     return 0;
    }
}

core::Status open_period(RotationSchedule kind, std::uint64_t custom_ns, std::int64_t utc_ns,
                         std::string_view base_dir, std::string_view file_name,
                         RotationScratch& scratch, int& fd_out) noexcept {
    return open_segment(kind, custom_ns, utc_ns, base_dir, file_name, 0U, scratch, fd_out);
}

core::Status open_segment(RotationSchedule kind, std::uint64_t custom_ns, std::int64_t utc_ns,
                          std::string_view base_dir, std::string_view file_name,
                          std::uint64_t segment, RotationScratch& scratch, int& fd_out) noexcept {
    fd_out = -1;
    if (!validate_segment_request(base_dir, file_name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    char effective_name[288];
    const std::size_t effective_len = segment_name(file_name, segment, effective_name);
    if (effective_len == 0U) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    // Ensure the base directory itself exists first (fresh deployment root;
    // EEXIST covers steady state). scratch.file is reused for the NUL-staged
    // base path — it is rebuilt in place below anyway.
    if (const core::Status base = prepare_base_directory(base_dir, scratch); !base) {
        return base;
    }

    // Directory: base_dir, or base_dir/<label> when rotating.
    if (const core::Status directory =
            build_rotation_dir(kind, custom_ns, utc_ns, base_dir, scratch);
        !directory) {
        return directory;
    }
    if (const core::Status created = ensure_directory(scratch.dir); !created) {
        return created;
    }

    // File: <dir>/<effective_name>, staged in the second fixed buffer.
    if (!build_segment_file(scratch, effective_name, effective_len)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return open_append(scratch.file, fd_out);
}

} // namespace nexigen::logging::detail
