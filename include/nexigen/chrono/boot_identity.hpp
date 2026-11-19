// nexigen::chrono — kernel boot identity for lossless telemetry (change
// add-time-base-service; spec chrono/time-base "Persistence and resolution";
// design D-I).
//
// Every JSONL line and every recorded session carries the 16-hex boot token so
// instants from different kernel boots can never be confused. The token is the
// first 8 bytes of `/proc/sys/kernel/random/boot_id`; the full UUID is written
// only to low-rate artefacts (the daemon history file, the recorder manifest).
//
// COLD PATH ONLY: reading the kernel file happens once per process and the
// result is cached; formatting allocates nothing.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nexigen::chrono {

/// @brief Length of the canonical 16-hex boot token text.
inline constexpr std::size_t boot_token_text_size = 16U;

/// @brief Kernel boot identity (token plus the full UUID when available).
///
/// @note Spec `chrono/time-base`, design D-I.
struct BootIdentity {
    std::array<char, 37> uuid{}; ///< Full UUID text, NUL-terminated; empty on failure.
    std::uint64_t token{0};      ///< First 8 bytes of the UUID, big-endian.
};

/// @brief Read the kernel boot identity.
///
/// Reads `/proc/sys/kernel/random/boot_id` once; a failed read leaves both
/// fields zero/empty. The call never throws and never allocates.
///
/// @return The observed identity; all-zero fields mean the file was unreadable.
/// @note Cold path only.
[[nodiscard]] BootIdentity read_boot_identity() noexcept;

/// @brief Cached 64-bit boot token of the current kernel boot.
///
/// @return The token, or `0` when the boot id could not be read.
/// @note Cold path only: the file is read once per process.
[[nodiscard]] std::uint64_t current_boot_token() noexcept;

/// @brief Render a boot token as 16 lowercase hex characters.
///
/// @param token Token to render.
/// @param out Destination buffer of at least 17 bytes (16 hex + NUL).
/// @return View over the 16 rendered characters.
/// @note Hot path safe: no allocation, no locale.
[[nodiscard]] std::string_view format_boot_token(std::uint64_t token,
                                                 char (&out)[boot_token_text_size + 1U]) noexcept;

/// @brief Parse a 16-hex boot token.
///
/// @param text Candidate token text; fewer than 16 hex digits is a miss.
/// @param out Destination, untouched on a miss.
/// @return `true` when exactly 16 hex characters parsed.
[[nodiscard]] bool parse_boot_token(std::string_view text, std::uint64_t& out) noexcept;

/// @brief Derive the 64-bit token from a full UUID.
///
/// Skips dashes and consumes the first 16 hex digits, so
/// `"8d1a2b3c-..."` yields `0x8d1a2b3c...`.
///
/// @param uuid UUID text; a short or malformed value yields whatever parsed.
/// @return The token.
[[nodiscard]] std::uint64_t boot_token_from_uuid(std::string_view uuid) noexcept;

} // namespace nexigen::chrono
