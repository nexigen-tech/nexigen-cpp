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

/// @brief Bounds-checked appender over caller-owned fixed storage.
///
/// Every append is bounds-checked; the first overflow attempt marks the writer
/// permanently failed and the caller skips the record or batch. There is no
/// reallocation, no allocation, and no exception path, so the class stages JSON
/// lines on hot paths. Escaping follows the strict JSON-line policy: `"` and
/// `\` in two-character form, `\b` `\t` `\n` `\f` `\r` in short form, other
/// bytes below `0x20` as `\u00XX`, and everything else as raw bytes so UTF-8
/// round-trips verbatim.
///
/// @note Spec `core/sink-plumbing`, requirement "Plumbing serialization scope":
///       the writer is plumbing and exposes no serialization surface of its
///       own (the `fixed_writer.hpp` exemption precedent).
class FixedWriter {
public:
    /// @brief Bind the writer to caller-owned storage.
    ///
    /// @param buf Caller-owned buffer written in place; it must outlive the
    ///        writer and not be written through another path meanwhile.
    /// @param cap Number of bytes writable at `buf`.
    FixedWriter(char* buf, std::size_t cap) noexcept : buf_(buf), cap_(cap) {}

    /// @brief Append one character.
    ///
    /// @param c Byte to append verbatim.
    /// @return `true` when the byte fit; `false` once the writer is failed,
    ///         without writing.
    bool append_char(char c) noexcept {
        if (!ok_ || len_ >= cap_) {
            ok_ = false;
            return false;
        }
        buf_[len_++] = c;
        return true;
    }

    /// @brief Append a string view verbatim.
    ///
    /// @param sv Bytes to append in order.
    /// @return `true` when all bytes fit; `false` when the writer is failed or
    ///         the view does not fit (nothing is partially appended).
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

    /// @brief Append an unsigned 64-bit decimal number.
    ///
    /// @param value Number to render.
    /// @return `true` when the digits fit; `false` otherwise.
    bool append_u64(std::uint64_t value) noexcept { return append_integral(value); }

    /// @brief Append a signed 64-bit decimal number.
    ///
    /// @param value Number to render.
    /// @return `true` when the digits fit; `false` otherwise.
    bool append_i64(std::int64_t value) noexcept { return append_integral(value); }

    /// @brief Append a `double` in `std::to_chars` shortest form.
    ///
    /// @param value Number to render.
    /// @return `true` when the digits fit; `false` on conversion failure or
    ///         overflow.
    bool append_double(double value) noexcept {
        char digits[40];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec != std::errc{}) {
            ok_ = false;
            return false;
        }
        return append_sv(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
    }

    /// @brief Append `sv` with the strict JSON-line escaping policy.
    ///
    /// @param sv Raw text to escape and append.
    /// @return `true` when every escaped byte fit; `false` once the writer is
    ///         failed (the caller then skips the record).
    /// @note Spec `core/sink-plumbing`, requirement "Plumbing serialization
    ///       scope"; the escape policy is shared by all JSON sinks.
    bool append_escaped(std::string_view sv) noexcept {
        for (const char c : sv) {
            append_escape_byte(static_cast<std::uint8_t>(c));
            if (!ok_) {
                return false;
            }
        }
        return ok_;
    }

    /// @brief Append a value as zero-padded, 16-character lowercase hex.
    ///
    /// @param value 64-bit identifier (trace/span id) to render.
    /// @return `true` when all 16 characters fit; `false` otherwise.
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

    /// @brief Number of bytes successfully appended so far.
    ///
    /// @return Current write offset into the caller-owned buffer.
    [[nodiscard]] std::size_t size() const noexcept { return len_; }
    /// @brief Whether every append so far succeeded.
    ///
    /// @return `false` permanently after the first overflow attempt.
    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    /// @brief Lowercase hex alphabet used by `append_hex16` and `\u00XX`.
    static constexpr std::string_view hex_digits = "0123456789abcdef";

    /// @brief Render an integer with `std::to_chars` and append the digits.
    ///
    /// @tparam Integer Integral type accepted by `std::to_chars`.
    /// @param value Number to render.
    /// @return `true` when the digits fit; `false` on conversion failure or
    ///         overflow.
    template <typename Integer> bool append_integral(Integer value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec != std::errc{}) {
            ok_ = false;
            return false;
        }
        return append_sv(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
    }

    /// @brief Append one escaped byte according to the JSON-line policy.
    ///
    /// @param b Byte to escape and append; may fail the writer on overflow.
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

    char* buf_;           ///< Caller-owned destination buffer (not owned).
    std::size_t cap_;     ///< Writable bytes at `buf_`.
    std::size_t len_ = 0; ///< Bytes appended so far.
    bool ok_ = true;      ///< Cleared permanently on the first overflow attempt.
};

} // namespace nexigen::core
