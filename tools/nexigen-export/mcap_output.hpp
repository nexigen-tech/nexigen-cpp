// nexigen-export — optional MCAP output adapter (no MCAP types in this header).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::exportc {

/// @brief Multi-channel MCAP writer for converter datasets.
class McapOutput {
public:
    /// @brief Open an empty MCAP file; add channels before writing messages.
    /// @param path Output file path.
    /// @return Writer, or `SystemError` when the file cannot be opened.
    [[nodiscard]] static core::Result<McapOutput> create(const std::string& path) noexcept;

    McapOutput(const McapOutput&) = delete;
    McapOutput& operator=(const McapOutput&) = delete;
    McapOutput(McapOutput&&) noexcept;
    McapOutput& operator=(McapOutput&&) noexcept;
    ~McapOutput();

    /// @brief Register one channel and preserve its declared encoding.
    /// @param topic Channel topic/name.
    /// @param schema_name Schema name; empty falls back to a generic name.
    /// @param schema_encoding Schema encoding (for example `jsonschema`/`protobuf`).
    /// @param schema_data Embedded descriptor bytes/text.
    /// @param message_encoding Message encoding (for example `json`/`protobuf`).
    /// @return Assigned channel id, or `SystemError`.
    [[nodiscard]] core::Result<std::uint16_t>
    add_channel(std::string_view topic, std::string_view schema_name,
                std::string_view schema_encoding, std::string_view schema_data,
                std::string_view message_encoding) noexcept;

    /// @brief Write one text (JSON) message.
    /// @return `true` on success.
    [[nodiscard]] bool write(std::uint16_t channel_id, std::uint32_t sequence,
                             std::uint64_t log_time_ns, std::uint64_t publish_time_ns,
                             std::string_view data) noexcept;

    /// @brief Write one opaque message without re-encoding its bytes.
    /// @return `true` on success.
    [[nodiscard]] bool write_bytes(std::uint16_t channel_id, std::uint32_t sequence,
                                   std::uint64_t log_time_ns, std::uint64_t publish_time_ns,
                                   std::span<const std::byte> data) noexcept;

    /// @brief Close the writer.
    /// @return `true` when the file was finalized.
    [[nodiscard]] bool close() noexcept;

    /// @brief Reopen a written MCAP and return its message count.
    /// @param path File to verify.
    /// @return Message count, or `CorruptData`.
    [[nodiscard]] static core::Result<std::uint64_t>
    verify(const std::string& path) noexcept;

private:
    struct Impl;
    explicit McapOutput(std::unique_ptr<Impl> impl) noexcept;

    [[nodiscard]] bool write_impl(std::uint16_t channel_id, std::uint32_t sequence,
                                  std::uint64_t log_time_ns, std::uint64_t publish_time_ns,
                                  const std::byte* data, std::size_t size) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace nexigen::exportc
