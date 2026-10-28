// nexigen::metrics — bounds-checked JSON writer (design D13).
//
// Fixed_writer stages into caller-owned fixed storage: every append is
// bounds-checked, a rejected append marks overflow and the caller skips the
// record. No reallocation, no exceptions, no allocation.
//
// Escaping mirrors the logging module's policy: '"' and '\\' in two-char
// form; '\b' '\t' '\n' '\f' '\r' in short form; other bytes < 0x20 as
// \u00XX; everything else passes through as raw bytes (UTF-8 round-trips
// verbatim inside valid JSON).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nexigen::metrics::detail {

/// Bounds-checked appender over caller memory.
class Fixed_writer {
public:
    Fixed_writer(char* buf, std::size_t cap) noexcept : buf_(buf), cap_(cap) {}

    bool append_char(char c) noexcept;
    bool append_sv(std::string_view sv) noexcept;
    bool append_u64(std::uint64_t value) noexcept;
    bool append_i64(std::int64_t value) noexcept;
    bool append_double(double value) noexcept;
    /// JSON-escape sv into the buffer (the policy above).
    bool append_escaped(std::string_view sv) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return len_; }
    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    char* buf_;
    std::size_t cap_;
    std::size_t len_ = 0;
    bool ok_ = true;
};

/// Scratch size for one JSON line: timestamp(30) + per-record payload. One
/// record carries identity (3 x 64) plus the summary fields, so 16 KiB is a
/// generous static bound.
inline constexpr std::size_t json_scratch_capacity = 16384;

} // namespace nexigen::metrics::detail
