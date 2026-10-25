// nexigen::logging — rotation implementation (design.md D11). See
// detail/rotation.hpp for semantics. Consumer-thread only; the buffers come
// from the caller's pre-allocated scratch, and the path building never
// touches the heap.
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/chrono/time_format.hpp>
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
    std::uint64_t v = neg ? static_cast<std::uint64_t>(-(value + 1)) + 1ULL
                          : static_cast<std::uint64_t>(value);
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

} // namespace

std::int64_t period_key(Rotation_kind kind, std::uint64_t custom_ns,
                        std::int64_t utc_ns) noexcept {
    switch (kind) {
        case Rotation_kind::Hourly: {
            // Same civil decomposition the labels use, so key changes and
            // visible boundaries cannot disagree.
            const chrono::Civil_time ct = chrono::civil_from_utc_ns(utc_ns);
            return chrono::days_from_utc_ns(utc_ns) * 24 + static_cast<std::int64_t>(ct.hour);
        }
        case Rotation_kind::Daily:
            return chrono::days_from_utc_ns(utc_ns);
        case Rotation_kind::Custom: {
            if (custom_ns == 0ULL) {
                return 0; // config error is surfaced at initialize(); stay monotone
            }
            const std::uint64_t u = static_cast<std::uint64_t>(utc_ns); // UTC >= epoch in practice
            return static_cast<std::int64_t>(u / custom_ns);
        }
        case Rotation_kind::None:
        default:
            return 0;
    }
}

std::size_t period_label(Rotation_kind kind, std::uint64_t custom_ns,
                         std::int64_t utc_ns, char (&buf)[64]) noexcept {
    buf[0] = '\0';
    switch (kind) {
        case Rotation_kind::Hourly:
        case Rotation_kind::Daily: {
            char iso[31];
            chrono::format_utc_iso8601_ns(utc_ns, iso);
            // Hourly: "YYYY-MM-DDTHH" (13 chars); Daily: "YYYY-MM-DD" (10).
            const std::size_t len = (kind == Rotation_kind::Hourly) ? 13U : 10U;
            std::memcpy(buf, iso, len);
            buf[len] = '\0';
            return len;
        }
        case Rotation_kind::Custom:
            return put_epoch_label(period_key(kind, custom_ns, utc_ns), buf);
        case Rotation_kind::None:
        default:
            return 0;
    }
}

core::Status open_period(Rotation_kind kind, std::uint64_t custom_ns, std::int64_t utc_ns,
                         std::string_view base_dir, std::string_view file_name,
                         Rotation_scratch& scratch, int& fd_out) noexcept {
    fd_out = -1;
    if (base_dir.empty() || file_name.empty() || base_dir.size() > 511 ||
        file_name.size() > 255 ||
        base_dir.find('\0') != std::string_view::npos ||
        file_name.find('\0') != std::string_view::npos) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    // Ensure the base directory itself exists first (fresh deployment root;
    // EEXIST covers steady state). scratch.file is reused for the NUL-staged
    // base path — it is rebuilt in place below anyway.
    std::size_t bpos = 0;
    if (!put(base_dir, scratch.file, bpos)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (::mkdirat(AT_FDCWD, scratch.file, 0755) != 0 && errno != EEXIST) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    // Directory: base_dir, or base_dir/<label> when rotating.
    std::size_t pos = 0;
    if (!put(base_dir, scratch.dir, pos)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (kind != Rotation_kind::None) {
        char label[64];
        const std::size_t label_len = period_label(kind, custom_ns, utc_ns, label);
        if (label_len == 0U || !put("/"sv, scratch.dir, pos) ||
            !put(std::string_view(label, label_len), scratch.dir, pos)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
    }

    if (::mkdirat(AT_FDCWD, scratch.dir, 0755) != 0 && errno != EEXIST) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    // File: <dir>/<file_name>, staged in the second fixed buffer.
    std::size_t fpos = 0;
    if (!put(std::string_view(scratch.dir), scratch.file, fpos) ||
        !put("/"sv, scratch.file, fpos) || !put(file_name, scratch.file, fpos)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    const int fd = ::openat(AT_FDCWD, scratch.file,
                            O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    fd_out = fd;
    return {};
}

} // namespace nexigen::logging::detail
