// nexigen-logview — JSON string escaping for export.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nexigen::logview {

/// @brief Escape one string into a caller buffer.
///
/// Escapes quote, backslash, and control bytes with `\u00XX`; all other UTF-8
/// bytes pass through unchanged.
///
/// @param text Source bytes.
/// @param out Destination buffer.
/// @param capacity Destination capacity including the terminating NUL.
/// @return Bytes written excluding the NUL, or `0` when the buffer is too small
///         (the destination is then left with an empty string).
[[nodiscard]] std::size_t json_escape(std::string_view text, char* out,
                                      std::size_t capacity) noexcept;

/// @brief Escape one string into owned text (cold path).
/// @param text Source bytes.
/// @return Escaped text.
[[nodiscard]] std::string json_escape(std::string_view text);

} // namespace nexigen::logview
