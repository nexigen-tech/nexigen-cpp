// nexigen storage-format benchmark — MCAP adapter implementation.
//
// This TU is compiled with exceptions enabled because MCAP's writer contains an
// explicit throw. Every public entry point catches at the boundary and returns
// a core::Result/boolean, so the benchmark's -fno-exceptions main never sees an
// exception cross the adapter.
#include "mcap_adapter.hpp"

#include <cstddef>
#include <cstring>
#include <exception>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

#include <mcap/mcap.hpp>

namespace nexigen::bench {

namespace {

constexpr std::string_view kSchemaName = "nexigen.bench.BenchPayload";
constexpr std::string_view kSchemaEncoding = "jsonschema";
constexpr std::string_view kSchemaData =
    R"({"type":"nexigen.bench.BenchPayload","layout":"nexigen.pod.v1","endianness":"little"})";
constexpr std::string_view kTopic = "nexigen/bench/payload";
constexpr std::string_view kMessageEncoding = "nexigen.pod";

class SinkWriter final : public mcap::IWritable {
public:
    explicit SinkWriter(Sink& sink) noexcept : sink_(sink) {}

    void end() override { sink_.flush(); }
    void flush() override {}
    [[nodiscard]] std::uint64_t size() const override { return size_; }

protected:
    void handleWrite(const std::byte* data, std::uint64_t size) override {
        sink_.write(data, static_cast<std::size_t>(size));
        size_ += size;
    }

private:
    Sink& sink_;
    std::uint64_t size_{0};
};

[[nodiscard]] bool read_opcode(std::ifstream& input, std::uint8_t& opcode) {
    input.read(reinterpret_cast<char*>(&opcode), sizeof(opcode));
    return input.gcount() == static_cast<std::streamsize>(sizeof(opcode));
}

[[nodiscard]] bool read_u64(std::ifstream& input, std::uint64_t& value) {
    unsigned char bytes[8]{};
    input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(bytes))) {
        return false;
    }
    value = 0;
    for (std::size_t i = 0; i < sizeof(bytes); ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (8U * i);
    }
    return true;
}

[[nodiscard]] bool read_u32(std::ifstream& input, std::uint32_t& value) {
    unsigned char bytes[4]{};
    input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(bytes))) {
        return false;
    }
    value = 0;
    for (std::size_t i = 0; i < sizeof(bytes); ++i) {
        value |= static_cast<std::uint32_t>(bytes[i]) << (8U * i);
    }
    return true;
}

[[nodiscard]] bool skip_bytes(std::ifstream& input, std::uint64_t size) {
    input.seekg(static_cast<std::streamoff>(size), std::ios::cur);
    return input.good();
}

void count_records_in_memory(const std::byte* data, std::size_t size, std::uint64_t& messages,
                             std::uint64_t& channels) {
    std::size_t offset = 0;
    while (offset + 9U <= size) {
        const std::uint8_t opcode = static_cast<std::uint8_t>(data[offset]);
        std::uint64_t length = 0;
        for (std::size_t i = 0; i < 8U; ++i) {
            length |= static_cast<std::uint64_t>(data[offset + 1U + i]) << (8U * i);
        }
        offset += 9U;
        if (offset + length > size) {
            return;
        }
        if (opcode == 0x05U) {
            ++messages;
        } else if (opcode == 0x04U) {
            ++channels;
        }
        offset += static_cast<std::size_t>(length);
    }
}

[[nodiscard]] bool scan_mcap(std::ifstream& input, std::uint64_t& messages,
                             std::uint64_t& channels) {
    char magic[8]{};
    input.read(magic, sizeof(magic));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(magic))) {
        return false;
    }
    while (true) {
        std::uint8_t opcode = 0;
        if (!read_opcode(input, opcode)) {
            return true;
        }
        std::uint64_t length = 0;
        if (!read_u64(input, length)) {
            return true;
        }
        const std::streamoff content_start = input.tellg();
        if (opcode == 0x02U) {
            return true;
        }
        if (opcode == 0x0FU) {
            return true;
        }
        if (opcode == 0x05U) {
            ++messages;
            if (!skip_bytes(input, length)) {
                return true;
            }
            continue;
        }
        if (opcode == 0x04U) {
            ++channels;
            if (!skip_bytes(input, length)) {
                return true;
            }
            continue;
        }
        if (opcode == 0x06U) {
            std::uint64_t start = 0;
            std::uint64_t end = 0;
            std::uint64_t uncompressed = 0;
            std::uint32_t crc = 0;
            std::uint32_t compression_size = 0;
            if (!read_u64(input, start) || !read_u64(input, end) ||
                !read_u64(input, uncompressed) || !read_u32(input, crc) ||
                !read_u32(input, compression_size)) {
                return true;
            }
            std::string compression(compression_size, '\0');
            input.read(compression.data(), static_cast<std::streamsize>(compression_size));
            if (input.gcount() != static_cast<std::streamsize>(compression_size)) {
                return true;
            }
            std::uint64_t records_size = 0;
            if (!read_u64(input, records_size)) {
                return true;
            }
            if (compression.empty()) {
                std::vector<std::byte> records(static_cast<std::size_t>(records_size));
                input.read(reinterpret_cast<char*>(records.data()),
                           static_cast<std::streamsize>(records_size));
                if (input.gcount() != static_cast<std::streamsize>(records_size)) {
                    return true;
                }
                count_records_in_memory(records.data(), records.size(), messages, channels);
            } else {
                if (!skip_bytes(input, records_size)) {
                    return true;
                }
            }
            const std::streamoff consumed = input.tellg() - content_start;
            if (consumed < 0 || static_cast<std::uint64_t>(consumed) > length) {
                return true;
            }
            if (!skip_bytes(input, length - static_cast<std::uint64_t>(consumed))) {
                return true;
            }
            continue;
        }
        if (!skip_bytes(input, length)) {
            return true;
        }
    }
}

} // namespace

struct McapBenchWriter::Impl {
    explicit Impl(Sink& sink) noexcept : output(sink) {}

    SinkWriter output;
    mcap::McapWriter writer;
    mcap::Schema schema;
    mcap::Channel channel;
    McapStats stats{};
};

McapBenchWriter::McapBenchWriter(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

McapBenchWriter::~McapBenchWriter() = default;

core::Result<std::unique_ptr<McapBenchWriter>>
McapBenchWriter::create(Sink& sink, const McapConfig& config) noexcept {
    try {
        auto impl = std::make_unique<Impl>(sink);
        mcap::McapWriterOptions options("nexigen-bench");
        options.noChunking = !config.chunked;
        options.chunkSize = config.chunked ? config.chunk_size : 0;
        options.compression = mcap::Compression::None;
        options.noMessageIndex = config.no_message_index;
        options.noSummary = config.no_summary;
        options.noChunkCRC = !config.chunk_crc;
        options.enableDataCRC = config.data_crc;
        options.noStatistics = false;

        impl->writer.open(impl->output, options);
        impl->schema = mcap::Schema(kSchemaName, kSchemaEncoding, kSchemaData);
        impl->writer.addSchema(impl->schema);
        impl->channel = mcap::Channel(kTopic, kMessageEncoding, impl->schema.id);
        impl->writer.addChannel(impl->channel);
        impl->stats.channels = 1;
        return std::unique_ptr<McapBenchWriter>(new McapBenchWriter(std::move(impl)));
    } catch (...) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
}

bool McapBenchWriter::write(const std::byte* payload, std::size_t size, std::uint32_t sequence,
                            std::uint64_t log_time_ns, std::uint64_t publish_time_ns) noexcept {
    if (!impl_ || impl_->stats.failed) {
        return false;
    }
    try {
        mcap::Message message{};
        message.channelId = impl_->channel.id;
        message.sequence = sequence;
        message.logTime = log_time_ns;
        message.publishTime = publish_time_ns;
        message.data = payload;
        message.dataSize = size;
        const mcap::Status status = impl_->writer.write(message);
        if (!status.ok()) {
            impl_->stats.failed = true;
            return false;
        }
        ++impl_->stats.messages;
        return true;
    } catch (...) {
        impl_->stats.failed = true;
        return false;
    }
}

bool McapBenchWriter::close() noexcept {
    if (!impl_) {
        return false;
    }
    try {
        impl_->writer.close();
        impl_->stats.bytes = impl_->output.size();
        return !impl_->stats.failed;
    } catch (...) {
        impl_->stats.failed = true;
        return false;
    }
}

McapStats McapBenchWriter::stats() const noexcept {
    if (!impl_) {
        return {};
    }
    return impl_->stats;
}

core::Result<McapVerify> verify_mcap(const std::string& path) noexcept {
    try {
        mcap::McapReader reader;
        const mcap::Status opened = reader.open(path);
        if (!opened.ok()) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        McapVerify verify{};
        auto view = reader.readMessages();
        for (auto it = view.begin(); it != view.end(); ++it) {
            ++verify.messages;
        }
        verify.channels = reader.channels().size();
        return verify;
    } catch (...) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
}

core::Result<McapVerify> recover_mcap(const std::string& path) noexcept {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    McapVerify verify{};
    if (!scan_mcap(input, verify.messages, verify.channels)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    verify.problems = input.eof() ? 1 : 0;
    return verify;
}

} // namespace nexigen::bench
