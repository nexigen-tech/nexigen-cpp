// nexigen storage-format benchmark — custom .nxrec prototype writer.
#include "custom_writer.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "crc32c_fast.hpp"

namespace nexigen::bench {

CustomRecWriter::CustomRecWriter(Sink& record_sink, Sink& index_sink, std::size_t buffer_size,
                                 std::uint64_t index_stride,
                                 std::uint64_t index_interval_ns) noexcept
    : record_sink_(record_sink)
    , index_sink_(index_sink)
    , buffer_(buffer_size)
    , index_stride_(index_stride)
    , index_interval_ns_(index_interval_ns) {}

void CustomRecWriter::append(const void* data, std::size_t size) noexcept {
    if (failed_) {
        return;
    }
    const auto* bytes = static_cast<const std::byte*>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const std::size_t space = buffer_.size() - used_;
        const std::size_t take = std::min(space, size - offset);
        std::memcpy(buffer_.data() + used_, bytes + offset, take);
        used_ += take;
        offset += take;
        if (used_ == buffer_.size()) {
            flush_buffer();
        }
    }
}

void CustomRecWriter::flush_buffer() noexcept {
    if (used_ == 0U) {
        return;
    }
    record_sink_.write(buffer_.data(), used_);
    used_ = 0;
}

void CustomRecWriter::emit_index(std::uint64_t end_offset) noexcept {
    PrototypeIndexEntry entry{};
    entry.ingest_ns = interval_start_ingest_;
    entry.rec_offset = interval_start_offset_;
    entry.min_event_ns = interval_min_event_;
    entry.max_event_ns = interval_max_event_;
    entry.end_rec_offset = end_offset;
    entry.record_count = interval_records_;
    entry.entry_crc32c = crc32c_fast(&entry, offsetof(PrototypeIndexEntry, entry_crc32c));
    index_sink_.write(reinterpret_cast<const std::byte*>(&entry), sizeof(entry));
    index_bytes_ += sizeof(entry);
    ++index_count_;
}

void CustomRecWriter::write(const std::byte* payload, std::size_t size, std::uint64_t sequence,
                            std::uint64_t event_ns, std::uint64_t ingest_ns) noexcept {
    if (failed_ || record_sink_.failed() || index_sink_.failed()) {
        failed_ = true;
        return;
    }

    PrototypeRecordHeader header{};
    header.payload_size = static_cast<std::uint32_t>(size);
    header.mono_ns = ingest_ns;
    header.sequence = sequence;
    header.header_crc32c =
        crc32c_fast(&header, offsetof(PrototypeRecordHeader, header_crc32c));
    header.payload_crc32c = crc32c_fast(payload, size);

    if (interval_records_ == 0U) {
        interval_start_offset_ = stream_offset_;
        interval_start_ingest_ = ingest_ns;
        interval_min_event_ = event_ns;
        interval_max_event_ = event_ns;
    } else {
        interval_min_event_ = std::min(interval_min_event_, event_ns);
        interval_max_event_ = std::max(interval_max_event_, event_ns);
    }

    append(&header, sizeof(header));
    append(payload, size);
    stream_offset_ += sizeof(header) + size;
    ++interval_records_;
    ++record_count_;

    const std::uint64_t interval_bytes = stream_offset_ - interval_start_offset_;
    if (interval_bytes >= index_stride_ ||
        (ingest_ns >= interval_start_ingest_ &&
         ingest_ns - interval_start_ingest_ >= index_interval_ns_)) {
        emit_index(stream_offset_);
        interval_records_ = 0U;
    }
}

void CustomRecWriter::finish() noexcept {
    flush_buffer();
    if (interval_records_ > 0U) {
        emit_index(stream_offset_);
        interval_records_ = 0U;
    }
    record_sink_.flush();
    index_sink_.flush();
}

namespace {

constexpr std::uint32_t kRecordMagic = 0x52434431U;
constexpr std::uint32_t kMaxPayload = 64U * 1024U * 1024U;

[[nodiscard]] bool read_record_header(std::ifstream& input, PrototypeRecordHeader& header) {
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    return input.gcount() == static_cast<std::streamsize>(sizeof(header));
}

} // namespace

core::Result<CustomVerify> verify_custom_rec(const std::string& rec_path,
                                             const std::string& idx_path) noexcept {
    std::ifstream input(rec_path, std::ios::binary);
    if (!input) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    CustomVerify verify{};
    verify.header_crc_ok = true;
    verify.payload_crc_ok = true;
    verify.index_ok = true;
    std::vector<std::byte> payload;
    PrototypeRecordHeader header{};
    while (read_record_header(input, header)) {
        if (header.magic != kRecordMagic || header.payload_size > kMaxPayload ||
            header.header_crc32c !=
                crc32c_fast(&header, offsetof(PrototypeRecordHeader, header_crc32c))) {
            verify.header_crc_ok = false;
            break;
        }
        payload.resize(header.payload_size);
        input.read(reinterpret_cast<char*>(payload.data()),
                   static_cast<std::streamsize>(payload.size()));
        if (input.gcount() != static_cast<std::streamsize>(payload.size())) {
            verify.payload_crc_ok = false;
            break;
        }
        if (header.payload_crc32c != crc32c_fast(payload.data(), payload.size())) {
            verify.payload_crc_ok = false;
            break;
        }
        ++verify.messages;
    }

    std::ifstream index(idx_path, std::ios::binary);
    if (!index) {
        verify.index_ok = false;
        return verify;
    }
    PrototypeIndexEntry entry{};
    while (index.read(reinterpret_cast<char*>(&entry), sizeof(entry))) {
        ++verify.index_entries;
        if (entry.entry_crc32c !=
            crc32c_fast(&entry, offsetof(PrototypeIndexEntry, entry_crc32c))) {
            verify.index_ok = false;
            break;
        }
        if (entry.record_count == 0U || entry.end_rec_offset <= entry.rec_offset) {
            verify.index_ok = false;
            break;
        }
        input.clear();
        input.seekg(static_cast<std::streamoff>(entry.rec_offset), std::ios::beg);
        PrototypeRecordHeader first{};
        if (!read_record_header(input, first) || first.magic != kRecordMagic) {
            verify.index_ok = false;
            break;
        }
    }
    return verify;
}

core::Result<CustomVerify> recover_custom_rec(const std::string& rec_path) noexcept {
    std::ifstream input(rec_path, std::ios::binary);
    if (!input) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    CustomVerify verify{};
    verify.header_crc_ok = true;
    verify.payload_crc_ok = true;
    verify.index_ok = true;
    std::vector<std::byte> payload;
    PrototypeRecordHeader header{};
    while (read_record_header(input, header)) {
        if (header.magic != kRecordMagic || header.payload_size > kMaxPayload ||
            header.header_crc32c !=
                crc32c_fast(&header, offsetof(PrototypeRecordHeader, header_crc32c))) {
            verify.header_crc_ok = false;
            break;
        }
        payload.resize(header.payload_size);
        input.read(reinterpret_cast<char*>(payload.data()),
                   static_cast<std::streamsize>(payload.size()));
        if (input.gcount() != static_cast<std::streamsize>(payload.size())) {
            break;
        }
        if (header.payload_crc32c != crc32c_fast(payload.data(), payload.size())) {
            verify.payload_crc_ok = false;
            break;
        }
        ++verify.messages;
    }
    return verify;
}

} // namespace nexigen::bench

