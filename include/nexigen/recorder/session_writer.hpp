// nexigen::recorder — preallocated raw session writer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/recorder/channel_declare.hpp>
#include <nexigen/recorder/record_shard.hpp>
#include <nexigen/recorder/schema_declare.hpp>
#include <nexigen/recorder/session_manifest.hpp>
#include <nexigen/recorder/storage_settings.hpp>
#include <nexigen/recorder/wire_types.hpp>

namespace nexigen::recorder {

/// @brief Single-threaded owner of one session's segment triplet.
///
/// All write buffers are allocated by `create()`. `append()` never allocates
/// and never blocks. The caller must declare a channel before appending to it.
class SessionWriter {
public:
    /// @brief Create the session directory and the first segment.
    /// @param settings Storage limits, ids, layout, and CRC mode.
    /// @return Writer, or a named validation/system error.
    [[nodiscard]] static core::Result<SessionWriter> create(const StorageSettings& settings) noexcept;

    SessionWriter(const SessionWriter&) = delete;
    SessionWriter& operator=(const SessionWriter&) = delete;
    SessionWriter(SessionWriter&&) noexcept;
    SessionWriter& operator=(SessionWriter&&) noexcept;
    ~SessionWriter();

    /// @brief Add a schema and write its declaration to the active segment.
    /// @param schema Embedded schema declaration.
    /// @return `Ok`, or a declaration/limit error.
    [[nodiscard]] core::Status declare_schema(const SchemaDeclare& schema) noexcept;

    /// @brief Add a channel and write its declaration to the active segment.
    /// @param channel Channel declaration.
    /// @return `Ok`, or a declaration/limit error.
    [[nodiscard]] core::Status declare_channel(const ChannelDeclare& channel) noexcept;

    /// @brief Declare a topic/schema pair, assigning a stable channel id.
    ///
    /// Reuses the id when the same topic and schema are already declared; a
    /// changed schema creates a new channel id.
    /// @param topic Topic/stream name.
    /// @param schema_id Declared schema id.
    /// @param kind Telemetry, opaque, or video dataset kind.
    /// @param message_encoding Payload message encoding.
    /// @param metadata Optional JSON metadata.
    /// @return Assigned channel id, or a declaration/limit error.
    [[nodiscard]] core::Result<std::uint32_t>
    declare_channel(std::string_view topic, std::uint32_t schema_id, DatasetKind kind,
                    std::string_view message_encoding,
                    std::string_view metadata = {}) noexcept;

    /// @brief Append one record. `payload` is copied into the write buffer.
    /// @param type Record type.
    /// @param channel_id Declared channel id.
    /// @param payload Record bytes.
    /// @param event_ns Event time stored in the index interval.
    /// @param sequence Publisher sequence.
    /// @param publisher_id Publisher identity.
    /// @return `Ok`, or a bounds/system error.
    [[nodiscard]] core::Status append(RecordType type, std::uint32_t channel_id,
                                      std::span<const std::byte> payload, std::uint64_t event_ns,
                                      std::uint64_t sequence,
                                      std::uint64_t publisher_id) noexcept;

    /// @brief Drain one producer shard into the active segment.
    /// @param shard Producer shard owned by one producer thread.
    /// @return `Ok`, or a failure when the segment cannot be written.
    [[nodiscard]] core::Status append_from_shard(RecordShard& shard) noexcept;

    /// @brief Record a monotonic-to-UTC calibration point.
    /// @param utc_offset_ns `utc_epoch_ns - commit_mono_ns` at the current instant.
    /// @return `Ok`, or a failure when the segment cannot be written.
    [[nodiscard]] core::Status append_time_sync(std::int64_t utc_offset_ns) noexcept;

    /// @brief Write a video frame into `.nxvid` and append its reference.
    ///
    /// The stream configuration is written before the first frame of a channel
    /// or when `config.config_epoch` changes.
    /// @param channel_id Declared video channel id.
    /// @param config Codec/dimensions/extradata declaration.
    /// @param reference Frame metadata; offsets/size are filled in.
    /// @param frame Encoded frame bytes (never parsed).
    /// @return `Ok`, or a bounds/system error.
    [[nodiscard]] core::Status append_video(std::uint32_t channel_id,
                                            const VideoStreamConfig& config,
                                            const VideoFrameRef& reference,
                                            std::span<const std::byte> frame) noexcept;

    /// @brief Finalize the active segment and open the next one.
    /// @return `Ok`, or a manifest/footer error.
    [[nodiscard]] core::Status rotate() noexcept;

    /// @brief Finalize the active segment and mark the session clean.
    /// @return `Ok`, or a manifest/footer error.
    [[nodiscard]] core::Status close() noexcept;

    /// @brief Current manifest snapshot.
    /// @return Manifest with the latest segment catalog.
    [[nodiscard]] const SessionManifest& manifest() const noexcept;
    /// @brief Sequence of the active segment.
    /// @return Segment sequence number.
    [[nodiscard]] std::uint32_t current_segment_seq() const noexcept;

private:
    struct Impl;
    explicit SessionWriter(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace nexigen::recorder
