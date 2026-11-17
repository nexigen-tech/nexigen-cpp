// nexigen storage-format benchmark — deterministic shared payload blob.
#include "payload_blob.hpp"

#include <cstring>

#include "crc32c_fast.hpp"

namespace nexigen::bench {

namespace {

constexpr std::uint64_t kSeed = 0x9E3779B97F4A7C15ULL;

[[nodiscard]] std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

} // namespace

std::size_t window_offset(std::uint64_t index) noexcept {
    return static_cast<std::size_t>((index * 7919ULL) % (PayloadBlob::kSize - PayloadBlob::kWindowReserve));
}

PayloadBlob PayloadBlob::create() {
    PayloadBlob blob;
    blob.data_.resize(kSize);
    std::uint64_t state = kSeed;
    std::size_t offset = 0;
    while (offset + sizeof(std::uint64_t) <= blob.data_.size()) {
        const std::uint64_t word = splitmix64(state);
        std::memcpy(blob.data_.data() + offset, &word, sizeof(word));
        offset += sizeof(word);
    }
    while (offset < blob.data_.size()) {
        blob.data_[offset] = static_cast<std::byte>(splitmix64(state) & 0xFFU);
        ++offset;
    }
    blob.checksum_ = crc32c_fast(blob.data_);
    return blob;
}

std::span<const std::byte> PayloadBlob::window(std::uint64_t index,
                                              std::size_t size) const noexcept {
    const std::size_t offset = window_offset(index);
    const std::size_t clamped = size <= kSize - offset ? size : kSize - offset;
    return std::span<const std::byte>(data_.data() + offset, clamped);
}

} // namespace nexigen::bench
