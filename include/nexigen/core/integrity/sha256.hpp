// nexigen::core::integrity — FIPS 180-4 SHA-256 (change
// add-integrity-primitives; promoted from nexigen::exec).
//
// Header-only so the base library stays an INTERFACE target. The hashing path
// allocates nothing; only the hex rendering and file hashing return strings.
#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>

namespace nexigen::core::integrity {

namespace detail {

/// @brief SHA-256 round constants.
inline constexpr std::array<std::uint32_t, 64> k_sha256_round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

/// @brief Rotate a 32-bit word right.
[[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t value, std::uint32_t count) noexcept {
    return (value >> count) | (value << (32U - count));
}

/// @brief Load a big-endian 32-bit word.
[[nodiscard]] constexpr std::uint32_t load_be32(const std::uint8_t* bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
}

/// @brief Store a big-endian 32-bit word.
inline void store_be32(std::uint8_t* bytes, std::uint32_t value) noexcept {
    bytes[0] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
    bytes[1] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[2] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[3] = static_cast<std::uint8_t>(value & 0xffU);
}

/// @brief Lowercase hex rendering of a 32-byte digest.
[[nodiscard]] inline std::string to_hex(const std::array<std::uint8_t, 32>& digest) {
    std::string out;
    out.resize(64U);
    static constexpr char k_digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < digest.size(); ++i) {
        out[i * 2U] = k_digits[(digest[i] >> 4U) & 0x0fU];
        out[i * 2U + 1U] = k_digits[digest[i] & 0x0fU];
    }
    return out;
}

} // namespace detail

/// @brief Incremental FIPS 180-4 SHA-256.
///
/// @note Spec `core/integrity`, requirement "SHA-256 in the base library".
class Sha256 {
public:
    Sha256() noexcept = default;

    /// @brief Feed bytes into the digest.
    void update(std::span<const std::uint8_t> bytes) noexcept {
        total_len_ += bytes.size();
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const std::size_t chunk = std::min(buffer_.size() - buffer_len_, bytes.size() - offset);
            for (std::size_t i = 0; i < chunk; ++i) {
                buffer_[buffer_len_ + i] = bytes[offset + i];
            }
            buffer_len_ += chunk;
            offset += chunk;
            if (buffer_len_ == buffer_.size()) {
                transform(buffer_.data());
                buffer_len_ = 0;
            }
        }
    }

    /// @brief Feed a text view.
    void update(std::string_view text) noexcept {
        update(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                             text.size()));
    }

    /// @brief Finalize and return the 32-byte digest.
    [[nodiscard]] std::array<std::uint8_t, 32> finish() noexcept {
        const std::uint64_t bit_len = total_len_ * 8U;
        std::array<std::uint8_t, 72> padding{};
        padding[0] = 0x80U;
        const std::size_t pad_len = buffer_len_ < 56U ? 56U - buffer_len_ : 120U - buffer_len_;
        update(std::span<const std::uint8_t>(padding.data(), pad_len));
        std::array<std::uint8_t, 8> length_bytes{};
        for (std::size_t i = 0; i < 8U; ++i) {
            length_bytes[7U - i] = static_cast<std::uint8_t>((bit_len >> (i * 8U)) & 0xffU);
        }
        update(length_bytes);
        std::array<std::uint8_t, 32> digest{};
        for (std::size_t i = 0; i < state_.size(); ++i) {
            detail::store_be32(digest.data() + i * 4U, state_[i]);
        }
        return digest;
    }

private:
    /// @brief Compress one 64-byte block.
    void transform(const std::uint8_t block[64]) noexcept {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16U; ++i) {
            words[i] = detail::load_be32(block + i * 4U);
        }
        for (std::size_t i = 16U; i < 64U; ++i) {
            const std::uint32_t s0 = detail::rotr(words[i - 15U], 7U) ^
                                     detail::rotr(words[i - 15U], 18U) ^ (words[i - 15U] >> 3U);
            const std::uint32_t s1 = detail::rotr(words[i - 2U], 17U) ^
                                     detail::rotr(words[i - 2U], 19U) ^ (words[i - 2U] >> 10U);
            words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
        }
        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (std::size_t i = 0; i < 64U; ++i) {
            const std::uint32_t s1 =
                detail::rotr(e, 6U) ^ detail::rotr(e, 11U) ^ detail::rotr(e, 25U);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t temp1 =
                h + s1 + choose + detail::k_sha256_round_constants[i] + words[i];
            const std::uint32_t s0 =
                detail::rotr(a, 2U) ^ detail::rotr(a, 13U) ^ detail::rotr(a, 22U);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint8_t, 64> buffer_{}; ///< Partial block.
    std::size_t buffer_len_{0};             ///< Bytes currently buffered.
    std::uint64_t total_len_{0};            ///< Total bytes hashed.
};

/// @brief SHA-256 of an in-memory buffer as lowercase hex.
[[nodiscard]] inline std::string sha256_hex(std::span<const std::uint8_t> bytes) {
    Sha256 hasher;
    hasher.update(bytes);
    return detail::to_hex(hasher.finish());
}

/// @brief SHA-256 of an in-memory string as lowercase hex.
[[nodiscard]] inline std::string sha256_hex(std::string_view text) {
    return sha256_hex(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

/// @brief SHA-256 of one file as lowercase hex.
///
/// @param path Absolute file path.
/// @return The digest, or `SecurityError` when the file cannot be opened or read.
[[nodiscard]] inline core::Result<std::string> sha256_file(std::string_view path) noexcept {
    const std::string file(path);
    const int fd = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(core::ErrorCode::SecurityError);
    }
    Sha256 hasher;
    std::array<std::uint8_t, 65536> buffer{};
    for (;;) {
        const ssize_t read_count = ::read(fd, buffer.data(), buffer.size());
        if (read_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            static_cast<void>(::close(fd));
            return std::unexpected(core::ErrorCode::SecurityError);
        }
        if (read_count == 0) {
            break;
        }
        hasher.update(
            std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(read_count)));
    }
    static_cast<void>(::close(fd));
    return detail::to_hex(hasher.finish());
}

} // namespace nexigen::core::integrity
