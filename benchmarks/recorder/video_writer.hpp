// nexigen storage-format benchmark — 4 KiB-aligned .nxvid blob writer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sink.hpp"

namespace nexigen::bench {

/// @brief Writes compressed frames into a page-aligned blob.
class VideoWriter {
public:
    static constexpr std::size_t kAlignment = 4096;

    explicit VideoWriter(Sink& sink) noexcept : sink_(sink) {}

    void write_frame(const std::byte* payload, std::size_t size, bool with_crc) noexcept;
    void finish() noexcept;

    /// @brief Preallocate frame bookkeeping before the measured region.
    void reserve(std::size_t frames) {
        offsets_.reserve(frames);
        crcs_.reserve(frames);
    }

    [[nodiscard]] std::uint64_t frame_count() const noexcept { return frame_count_; }
    [[nodiscard]] std::uint64_t bytes() const noexcept { return sink_.bytes_written(); }
    [[nodiscard]] const std::vector<std::uint64_t>& offsets() const noexcept { return offsets_; }
    [[nodiscard]] const std::vector<std::uint32_t>& crcs() const noexcept { return crcs_; }
    [[nodiscard]] bool failed() const noexcept { return sink_.failed(); }

private:
    void pad_to_alignment() noexcept;

    Sink& sink_;
    std::uint64_t frame_count_{0};
    std::vector<std::uint64_t> offsets_{};
    std::vector<std::uint32_t> crcs_{};
    bool header_written_{false};
};

} // namespace nexigen::bench
