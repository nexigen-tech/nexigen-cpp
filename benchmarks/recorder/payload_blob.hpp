// nexigen storage-format benchmark — deterministic shared payload blob.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace nexigen::bench {

/// @brief The 16 MiB deterministic payload blob every candidate consumes.
class PayloadBlob {
public:
    static constexpr std::size_t kSize = 16U * 1024U * 1024U;
    static constexpr std::size_t kWindowReserve = 512U * 1024U;

    /// @brief Generate the blob from the fixed seed.
    [[nodiscard]] static PayloadBlob create();

    /// @brief The window a message with index `i` and `size` bytes consumes.
    [[nodiscard]] std::span<const std::byte> window(std::uint64_t index,
                                                    std::size_t size) const noexcept;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return data_; }

    [[nodiscard]] std::uint32_t checksum() const noexcept { return checksum_; }

private:
    std::vector<std::byte> data_;
    std::uint32_t checksum_{0};
};

/// @brief The design's deterministic window offset.
[[nodiscard]] std::size_t window_offset(std::uint64_t index) noexcept;

} // namespace nexigen::bench
