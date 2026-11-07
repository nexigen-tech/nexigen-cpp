// nexigen::core — bounds-checked JSON FixedWriter shared by logging /
// metrics / tracing (change fix-p2-observability-and-writers, analysis
// #24: one implementation instead of three near-identical copies). The
// class lives in nexigen::core directly (NOT core::detail): importing
// another core::detail namespace into TUs that already `using namespace
// nexigen::core` + a module namespace would make the bare `detail:`
// name ambiguous in test scopes — Nexigen guard noted in the module shims.
//
// FixedWriter stages into caller-owned fixed storage: every append is
// bounds-checked, a rejected append marks overflow permanently and the
// caller skips the record or batch. No reallocation, no exceptions, no
// allocation. The write policy mirrors the per-module "strict JSON line"
// contracts that were checked against duplicate copies before:
//
// Escaping policy: '"' and '\\' in two-char form; '\b' '\t' '\n' '\f' '\r'
// in short form; other bytes < 0x20 as \u00XX; everything else passes
// through as raw bytes — so UTF-8 (Cyrillic included) round-trips verbatim
// inside valid JSON.
//
// Consumed through the per-module `detail/json_writer.hpp` aliases, which
// also keep their own scratch-size contracts (4096 / 16384 / 8192).
// Header-only by design — the bodies are inline so no cargo TU exists.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>

#include <charconv>

namespace nexigen::core {

/// Bounds-checked appender over caller memory. `ok()` goes false permanently
/// on the first overflow attempt; nothing else changes (no UB, no writes).
class FixedWriter {
public:
    FixedWriter(char* buf, std::size_t cap) noexcept : buf_(buf), cap_(cap) {}

    bool append_char(char c) noexcept {
        if (!ok_ || len_ >= cap_) {
            ok_ = false;
            return false;
        }
        buf_[len_++] = c;
        return true;
    }

    bool append_sv(std::string_view sv) noexcept {
        if (!ok_ || sv.size() > cap_ - len_) {
            ok_ = false;
            return false;
        }
        for (const char c : sv) {
            buf_[len_++] = c;
        }
        return true;
    }

    bool append_u64(std::uint64_t value) noexcept { return append_integral(value); }

    bool append_i64(std::int64_t value) noexcept { return append_integral(value); }

    bool append_double(double value) noexcept {
        char digits[40];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec != std::errc{}) {
            ok_ = false;
            return false;
        }
        return append_sv(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
    }

    /// JSON-escape sv into the buffer (the policy above).
    bool append_escaped(std::string_view sv) noexcept {
        for (const char c : sv) {
            append_escape_byte(static_cast<std::uint8_t>(c));
            if (!ok_) {
                return false;
            }
        }
        return ok_;
    }

    /// Zero-padded 16-char lowercase hex (trace/span identifiers).
    bool append_hex16(std::uint64_t value) noexcept {
        if (!ok_ || (cap_ - len_) < 16U) {
            ok_ = false;
            return false;
        }
        for (std::size_t i = 0; i < 16U; ++i) {
            // Most significant nibble first: nibble i occupies bits 60-4i.
            const unsigned shift = 60U - static_cast<unsigned>(i) * 4U;
            buf_[len_++] = hex_digits[(value >> shift) & 0xFULL];
        }
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return len_; }
    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    static constexpr std::string_view hex_digits = "0123456789abcdef";

    template <typename Integer> bool append_integral(Integer value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec != std::errc{}) {
            ok_ = false;
            return false;
        }
        return append_sv(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
    }

    void append_escape_byte(std::uint8_t b) noexcept {
        switch (b) {
            case '"':
                static_cast<void>(append_char('\\'));
                static_cast<void>(append_char('"'));
                return;
            case '\\':
                static_cast<void>(append_char('\\'));
                static_cast<void>(append_char('\\'));
                return;
            case '\b':
                static_cast<void>(append_char('\\'));
                static_cast<void>(append_char('b'));
                return;
            case '\f':
                static_cast<void>(append_char('\\'));
                static_cast<void>(append_char('f'));
                return;
            case '\n':
                static_cast<void>(append_char('\\'));
                static_cast<void>(append_char('n'));
                return;
            case '\r':
                static_cast<void>(append_char('\\'));
                static_cast<void>(append_char('r'));
                return;
            case '\t':
                static_cast<void>(append_char('\\'));
                static_cast<void>(append_char('t'));
                return;
            default: break;
        }
        if (b < 0x20U) {
            static_cast<void>(append_char('\\'));
            static_cast<void>(append_char('u'));
            static_cast<void>(append_char('0'));
            static_cast<void>(append_char('0'));
            static_cast<void>(append_char(hex_digits[(b >> 4U) & 0x0FU]));
            static_cast<void>(append_char(hex_digits[b & 0x0FU]));
            return;
        }
        static_cast<void>(append_char(static_cast<char>(b))); // UTF-8 pass-through
    }

    char* buf_;
    std::size_t cap_;
    std::size_t len_ = 0;
    bool ok_ = true;
};

} // namespace nexigen::core
