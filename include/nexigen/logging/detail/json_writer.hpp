// nexigen::logging — strict JSON line formatter (design.md D12, spec
// logging/file-logger "Strict JSON record schema").
//
// Fixed_writer stages into caller-owned fixed storage: every append is
// bounds-checked, a rejected append marks overflow, the caller skips the
// record. No reallocation, no exceptions.
//
// format_json_line emits the EXACT schema and field order:
//   {"timestamp":"YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ","level":"<lowercase>",
//    "app":"<escaped>","thread_id":<int>,
//    "message":"[<basename>:<line>] (<function>) <user message>"}\n
//
// Escaping: '"' and '\\' two-char form; '\b' '\t' '\n' '\f' '\r' short forms;
// other bytes < 0x20 as \u00XX; everything else passes through as raw bytes —
// so UTF-8 (Cyrillic included) round-trips verbatim inside valid JSON.
//
// The function name is written exactly as std::source_location reports it
// (GCC's readable __PRETTY_FUNCTION__-style text, no libiberty demangling
// here — design Open Questions).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging::detail {

/// Bounds-checked appender over caller memory. `ok()` goes false permanently
/// on the first overflow attempt; nothing else changes (no UB, no writes).
class Fixed_writer {
public:
    Fixed_writer(char* buf, std::size_t cap) noexcept : buf_(buf), cap_(cap) {}

    bool append_char(char c) noexcept;
    bool append_sv(std::string_view sv) noexcept;
    bool append_u64(std::uint64_t value) noexcept;
    /// JSON-escape sv into the buffer (policy in the header comment).
    bool append_escaped(std::string_view sv) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return len_; }
    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    char* buf_;
    std::size_t cap_;
    std::size_t len_ = 0;
    bool ok_ = true;
};

/// Scratch size for one line: timestamp(29)+decoration(~70)+app(≤64)+
/// tid(≤10)+function name (bounded by escaping) + message (≤128×6 worst-case
/// escape expansion) — 4 KiB is a generous static bound (design D12).
inline constexpr std::size_t json_scratch_capacity = 4096;

/// Format one record line into scratch. Returns the byte length (a full line
/// including trailing '\n'), or 0 when the line would not fit — the caller
/// then skips the record (a malformed/overflowing record must never emit a
/// half line into the file).
[[nodiscard]] std::size_t format_json_line(std::int64_t utc_ns, LogLevel level,
                                           std::string_view app, std::uint32_t thread_id,
                                           std::string_view file_name, std::uint32_t line,
                                           std::string_view function_name,
                                           std::string_view message,
                                           char (&scratch)[json_scratch_capacity]) noexcept;

} // namespace nexigen::logging::detail
