// nexigen storage-format benchmark — 4 KiB-aligned .nxvid blob writer.
#include "video_writer.hpp"

#include <array>

#include "crc32c_fast.hpp"

namespace nexigen::bench {

namespace {
constexpr std::array<std::byte, 64> kBlobHeader{};
} // namespace

void VideoWriter::pad_to_alignment() noexcept {
    const std::uint64_t current = sink_.bytes_written();
    const std::size_t remainder = current % kAlignment;
    const std::size_t padding = remainder == 0U ? 0U : kAlignment - remainder;
    std::array<std::byte, kAlignment> zeros{};
    sink_.write(zeros.data(), padding);
}

void VideoWriter::write_frame(const std::byte* payload, std::size_t size, bool with_crc) noexcept {
    if (!header_written_) {
        sink_.write(kBlobHeader.data(), kBlobHeader.size());
        pad_to_alignment();
        header_written_ = true;
    } else {
        pad_to_alignment();
    }
    offsets_.push_back(sink_.bytes_written());
    sink_.write(payload, size);
    if (with_crc) {
        crcs_.push_back(crc32c_fast(payload, size));
    }
    ++frame_count_;
}

void VideoWriter::finish() noexcept {
    sink_.flush();
}

} // namespace nexigen::bench
