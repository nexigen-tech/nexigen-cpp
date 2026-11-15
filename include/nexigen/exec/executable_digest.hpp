// nexigen::exec — executable integrity digest (change p2-exec-platform-controls;
// spec exec/trusted-executable).
//
// COLD PATH ONLY. SHA-256 over a file or an in-memory buffer, used by the
// pre-fork trust gate. No allocation on the update path; the final hex string
// is the only allocation.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::exec {

/// @brief Incremental FIPS 180-4 SHA-256.
class Sha256 {
public:
    Sha256() noexcept = default;

    /// @brief Feed bytes into the digest.
    void update(std::span<const std::uint8_t> bytes) noexcept;

    /// @brief Finalize and return the 32-byte digest.
    [[nodiscard]] std::array<std::uint8_t, 32> finish() noexcept;

private:
    void transform(const std::uint8_t block[64]) noexcept;

    std::array<std::uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_len_{0};
    std::uint64_t total_len_{0};
};

/// @brief SHA-256 of an in-memory buffer as lowercase hex.
[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> bytes);

/// @brief SHA-256 of an in-memory string as lowercase hex.
[[nodiscard]] std::string sha256_hex(std::string_view text);

/// @brief SHA-256 of one file as lowercase hex.
///
/// @param path Absolute executable path.
/// @return The digest, or `SecurityError` when the file cannot be opened or read.
[[nodiscard]] core::Result<std::string> sha256_file(std::string_view path) noexcept;

} // namespace nexigen::exec
