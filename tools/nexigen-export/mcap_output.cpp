// nexigen-export — optional MCAP output adapter implementation.
#include "mcap_output.hpp"

#include <deque>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include <mcap/mcap.hpp>

namespace nexigen::exportc {

struct McapOutput::Impl {
    mcap::McapWriter writer;
    std::deque<mcap::Schema> schemas;
    std::deque<mcap::Channel> channels;
    bool failed{false};
};

McapOutput::McapOutput(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
McapOutput::McapOutput(McapOutput&&) noexcept = default;
McapOutput& McapOutput::operator=(McapOutput&&) noexcept = default;
McapOutput::~McapOutput() = default;

core::Result<McapOutput> McapOutput::create(const std::string& path) noexcept {
    try {
        auto impl = std::make_unique<Impl>();
        mcap::McapWriterOptions options("nexigen-export");
        options.compression = mcap::Compression::None;
        const auto opened = impl->writer.open(path, options);
        if (!opened.ok()) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return McapOutput(std::move(impl));
    } catch (...) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
}

core::Result<std::uint16_t> McapOutput::add_channel(std::string_view topic,
                                                    std::string_view schema_name,
                                                    std::string_view schema_encoding,
                                                    std::string_view schema_data,
                                                    std::string_view message_encoding) noexcept {
    if (!impl_ || impl_->failed) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    try {
        const std::string name =
            schema_name.empty() ? std::string("nexigen.export.Record") : std::string(schema_name);
        const std::string encoding =
            schema_encoding.empty() ? std::string("jsonschema") : std::string(schema_encoding);
        const std::string data =
            schema_data.empty() ? std::string(R"({"type":"object"})") : std::string(schema_data);
        const std::string messages =
            message_encoding.empty() ? std::string("json") : std::string(message_encoding);
        impl_->schemas.emplace_back(name, encoding, data);
        impl_->writer.addSchema(impl_->schemas.back());
        impl_->channels.emplace_back(topic, messages, impl_->schemas.back().id);
        impl_->writer.addChannel(impl_->channels.back());
        return impl_->channels.back().id;
    } catch (...) {
        impl_->failed = true;
        return std::unexpected(core::ErrorCode::SystemError);
    }
}

bool McapOutput::write(std::uint16_t channel_id, std::uint32_t sequence, std::uint64_t log_time_ns,
                       std::uint64_t publish_time_ns, std::string_view data) noexcept {
    return write_impl(channel_id, sequence, log_time_ns, publish_time_ns,
                      reinterpret_cast<const std::byte*>(data.data()), data.size());
}

bool McapOutput::write_bytes(std::uint16_t channel_id, std::uint32_t sequence,
                             std::uint64_t log_time_ns, std::uint64_t publish_time_ns,
                             std::span<const std::byte> data) noexcept {
    return write_impl(channel_id, sequence, log_time_ns, publish_time_ns, data.data(), data.size());
}

bool McapOutput::write_impl(std::uint16_t channel_id, std::uint32_t sequence,
                            std::uint64_t log_time_ns, std::uint64_t publish_time_ns,
                            const std::byte* data, std::size_t size) noexcept {
    if (!impl_ || impl_->failed) {
        return false;
    }
    try {
        mcap::Message message{};
        message.channelId = channel_id;
        message.sequence = sequence;
        message.logTime = log_time_ns;
        message.publishTime = publish_time_ns;
        message.data = data;
        message.dataSize = size;
        const auto status = impl_->writer.write(message);
        if (!status.ok()) {
            impl_->failed = true;
            return false;
        }
        return true;
    } catch (...) {
        impl_->failed = true;
        return false;
    }
}

bool McapOutput::close() noexcept {
    if (!impl_ || impl_->failed) {
        return false;
    }
    try {
        impl_->writer.close();
        return true;
    } catch (...) {
        impl_->failed = true;
        return false;
    }
}

core::Result<std::uint64_t> McapOutput::verify(const std::string& path) noexcept {
    try {
        mcap::McapReader reader;
        const auto opened = reader.open(path);
        if (!opened.ok()) {
            return std::unexpected(core::ErrorCode::CorruptData);
        }
        std::uint64_t messages = 0;
        auto view = reader.readMessages();
        for (auto it = view.begin(); it != view.end(); ++it) {
            ++messages;
        }
        if (reader.channels().empty()) {
            return std::unexpected(core::ErrorCode::CorruptData);
        }
        return messages;
    } catch (...) {
        return std::unexpected(core::ErrorCode::CorruptData);
    }
}

} // namespace nexigen::exportc
