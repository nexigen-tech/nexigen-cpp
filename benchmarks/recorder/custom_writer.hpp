// nexigen storage-format benchmark — custom .nxrec prototype writer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <nexigen/core/result.hpp>

#include "sink.hpp"

namespace nexigen::bench {

/// @brief Prototype record header from the recorder design (48 bytes).
struct alignas(8) PrototypeRecordHeader {
    std::uint32_t magic{0x52434431U};
    std::uint16_t type{1};
    std::uint16_t flags{0};
    std::uint32_t channel_id{0};
    std::uint32_t payload_size{0};
    std::uint64_t mono_ns{0};
    std::uint64_t sequence{0};
    std::uint64_t publisher_id{1};
    std::uint32_t header_crc32c{0};
    std::uint32_t payload_crc32c{0};
};
static_assert(sizeof(PrototypeRecordHeader) == 48);
static_assert(alignof(PrototypeRecordHeader) == 8);
static_assert(std::is_trivially_copyable_v<PrototypeRecordHeader>);

/// @brief Prototype sparse index entry (64 bytes).
struct alignas(8) PrototypeIndexEntry {
    std::uint64_t ingest_ns{0};
    std::uint64_t rec_offset{0};
    std::uint64_t min_event_ns{0};
    std::uint64_t max_event_ns{0};
    std::uint64_t channel_mask{1};
    std::uint64_t end_rec_offset{0};
    std::uint32_t record_count{0};
    std::uint32_t flags{0};
    std::uint32_t entry_crc32c{0};
    std::uint32_t reserved{0};
};
static_assert(sizeof(PrototypeIndexEntry) == 64);
static_assert(alignof(PrototypeIndexEntry) == 8);
static_assert(std::is_trivially_copyable_v<PrototypeIndexEntry>);

inline constexpr std::uint64_t kIndexStride = 256U * 1024U;
inline constexpr std::uint64_t kIndexIntervalNs = 50'000'000ULL;

/// @brief Batched .nxrec prototype writer.
class CustomRecWriter {
public:
    CustomRecWriter(Sink& record_sink, Sink& index_sink, std::size_t buffer_size,
                    std::uint64_t index_stride, std::uint64_t index_interval_ns) noexcept;

    void write(const std::byte* payload, std::size_t size, std::uint64_t sequence,
               std::uint64_t event_ns, std::uint64_t ingest_ns) noexcept;

    void finish() noexcept;

    [[nodiscard]] std::uint64_t record_count() const noexcept { return record_count_; }
    [[nodiscard]] std::uint64_t index_count() const noexcept { return index_count_; }
    [[nodiscard]] std::uint64_t index_offset() const noexcept { return index_bytes_; }
    [[nodiscard]] bool failed() const noexcept {
        return failed_ || record_sink_.failed() || index_sink_.failed();
    }

private:
    void flush_buffer() noexcept;
    void append(const void* data, std::size_t size) noexcept;
    void emit_index(std::uint64_t end_offset) noexcept;

    Sink& record_sink_;
    Sink& index_sink_;
    std::vector<std::byte> buffer_;
    std::size_t used_{0};
    std::uint64_t stream_offset_{0};
    std::uint64_t index_bytes_{0};
    std::uint64_t index_stride_{0};
    std::uint64_t index_interval_ns_{0};
    std::uint64_t interval_start_offset_{0};
    std::uint64_t interval_start_ingest_{0};
    std::uint64_t interval_min_event_{0};
    std::uint64_t interval_max_event_{0};
    std::uint32_t interval_records_{0};
    std::uint64_t record_count_{0};
    std::uint64_t index_count_{0};
    bool failed_{false};
};

/// @brief Verification result for the custom prototype.
struct CustomVerify {
    std::uint64_t messages{0};
    std::uint64_t index_entries{0};
    bool header_crc_ok{false};
    bool payload_crc_ok{false};
    bool index_ok{false};
};

/// @brief Reopen a .nxrec/.nxidx pair and verify headers, payloads, and index.
[[nodiscard]] core::Result<CustomVerify> verify_custom_rec(const std::string& rec_path,
                                                           const std::string& idx_path) noexcept;

/// @brief Read a possibly truncated .nxrec and return the intact prefix.
[[nodiscard]] core::Result<CustomVerify> recover_custom_rec(const std::string& rec_path) noexcept;

} // namespace nexigen::bench

