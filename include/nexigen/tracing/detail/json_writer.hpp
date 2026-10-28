// nexigen::tracing — bounds-checked JSON writer (design D2/D11; mirrors the
// logging/metrics Fixed_writer policy).
//
// Fixed_writer stages into caller-owned fixed storage: every append is
// bounds-checked, a rejected append marks overflow, the caller skips the
// record. No reallocation, no exceptions, no allocation.
//
// Escaping policy: '"' and '\\' in two-char form; '\b' '\t' '\n' '\f' '\r'
// in short form; other bytes < 0x20 as \u00XX; everything else passes
// through as raw bytes — so UTF-8 (Cyrillic included) round-trips verbatim
// inside valid JSON.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nexigen::tracing::detail {

/// Bounds-checked appender over caller memory. `ok()` goes false permanently
/// on the first overflow attempt; nothing else changes (no UB, no writes).
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
    /// Zero-padded 16-char lowercase hex (trace/span identifiers).
    bool append_hex16(std::uint64_t value) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return len_; }
    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    char* buf_;
    std::size_t cap_;
    std::size_t len_ = 0;
    bool ok_ = true;
};

/// Scratch size for one JSON line: two ISO-8601 stamps (30 each), ids and
/// names with worst-case escaping, the location trio, and four attributes —
/// 8 KiB is a generous static bound (design D11).
inline constexpr std::size_t json_scratch_capacity = 8192;

} // namespace nexigen::tracing::detail
