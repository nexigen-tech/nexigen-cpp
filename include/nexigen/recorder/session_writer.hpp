/// @file session_writer.hpp
/// @brief Preallocated single-writer raw session writer.
///
/// @note Spec `recorder/raw-storage`, requirements "Preallocated allocation-free
///       write path", "Record framing and CRC-32C", "Rotation and retention",
///       and "Crash recovery"; spec `recorder/video-storage`, requirements
///       "`.nxvid` blob layout" and "Optional per-frame CRC".
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
///
/// @note Spec `recorder/raw-storage`, scenario "Single writer owns the stream";
///       producers hand records through `RecordShard` and only this object owns
///       append order, rotation, retention, and the sparse index.
class SessionWriter {
public:
    /// @brief Create the session directory and the first segment.
    /// @param settings Storage limits, ids, layout, and CRC mode.
    /// @return Writer, or `InvalidArgument`/`UnsupportedFormat`/`SystemError`.
    /// @note Spec `recorder/raw-storage`, requirement "Session manifest and
    ///       segment triplet"; the manifest is written atomically before the
    ///       writer is returned.
    [[nodiscard]] static core::Result<SessionWriter>
    create(const StorageSettings& settings) noexcept;

    /// @brief Move ownership of the writer and its open segment descriptors.
    /// @param other Writer to move from; it becomes empty.
    SessionWriter(SessionWriter&& other) noexcept;
    /// @brief Move-assign ownership, closing any currently owned session.
    /// @param other Writer to move from; it becomes empty.
    /// @return This writer.
    SessionWriter& operator=(SessionWriter&& other) noexcept;
    /// @brief Close the session if it is still open.
    ~SessionWriter();

    SessionWriter(const SessionWriter&) = delete;
    SessionWriter& operator=(const SessionWriter&) = delete;

    /// @brief Add a schema and write its declaration to the active segment.
    /// @param schema Embedded schema declaration.
    /// @return `Ok`, or a declaration/limit error.
    /// @note Spec `recorder/raw-storage`, requirement "Embedded schema and
    ///       channel registry"; the full descriptor is embedded and mirrored in
    ///       `session.json`.
    [[nodiscard]] core::Status declare_schema(const SchemaDeclare& schema) noexcept;

    /// @brief Add a channel and write its declaration to the active segment.
    /// @param channel Channel declaration.
    /// @return `Ok`, or a declaration/limit error.
    /// @note The reference must be declared before the first record that uses
    ///       it; a schema change creates a new channel id.
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
    /// @note Reuses the existing id for an identical topic/schema pair, which
    ///       keeps reconnects from fragmenting the dataset inventory.
    [[nodiscard]] core::Result<std::uint32_t>
    declare_channel(std::string_view topic, std::uint32_t schema_id, DatasetKind kind,
                    std::string_view message_encoding, std::string_view metadata = {}) noexcept;

    /// @brief Append one record. `payload` is copied into the write buffer.
    /// @param type Record type.
    /// @param channel_id Declared channel id.
    /// @param payload Record bytes.
    /// @param event_ns Event time stored in the index interval.
    /// @param sequence Publisher sequence.
    /// @param publisher_id Publisher identity.
    /// @return `Ok`, or a bounds/system error.
    /// @note Spec `recorder/raw-storage`, requirements "Preallocated allocation-
    ///       free write path" and "Rotation and retention"; the payload is
    ///       copied into the preallocated buffer and the byte cap covers
    ///       `.nxrec` plus `.nxvid`.
    [[nodiscard]] core::Status append(RecordType type, std::uint32_t channel_id,
                                      std::span<const std::byte> payload, std::uint64_t event_ns,
                                      std::uint64_t sequence, std::uint64_t publisher_id) noexcept;

    /// @brief Drain one producer shard into the active segment.
    /// @param shard Producer shard owned by one producer thread.
    /// @return `Ok`, or a failure when the segment cannot be written.
    /// @note A full shard is represented by one `GapMarker` with reason
    ///       `IoBackpressure`; draining never blocks the producer.
    [[nodiscard]] core::Status append_from_shard(RecordShard& shard) noexcept;

    /// @brief Record a monotonic-to-UTC calibration point.
    /// @param utc_offset_ns `utc_epoch_ns - commit_mono_ns` at the current instant.
    /// @return `Ok`, or a failure when the segment cannot be written.
    /// @note Spec `recorder/raw-storage`, requirement "Time and sequence
    ///       mapping"; the converter derives UTC from this record.
    [[nodiscard]] core::Status append_time_sync(std::int64_t utc_offset_ns) noexcept;

    /// @brief Record one published time-base change (domain history entry).
    /// @param entry History entry with rows, boot identity and epoch binding.
    /// @return `Ok`, or a failure when the segment cannot be written.
    /// @note Spec `recorder/raw-storage`, requirement "Time-base history
    ///       persistence"; the entry is appended verbatim, so the exporter
    ///       resolves native instants without the original daemon.
    [[nodiscard]] core::Status append_time_base(const TimeBasePayload& entry) noexcept;

    /// @brief Record one 1 Hz time-service snapshot.
    /// @param snapshot Snapshot with every domain value, epoch, and flag.
    /// @return `Ok`, or a failure when the segment cannot be written.
    /// @note Spec `recorder/raw-storage`, requirement "TimeSnapshot record"; the
    ///       snapshot never duplicates the coefficient table, so a reader pairs
    ///       it with the `TimeBase` entry of the same generation.
    [[nodiscard]] core::Status append_time_snapshot(const chrono::TimeSnapshot& snapshot) noexcept;

    /// @brief Write a video frame into `.nxvid` and append its reference.
    ///
    /// The stream configuration is written before the first frame of a channel
    /// or when `config.config_epoch` changes.
    /// @param channel_id Declared video channel id.
    /// @param config Codec/dimensions/extradata declaration.
    /// @param reference Frame metadata; offsets/size are filled in.
    /// @param frame Encoded frame bytes (never parsed).
    /// @return `Ok`, or a bounds/system error.
    /// @note Spec `recorder/video-storage`, requirements "`.nxvid` blob layout"
    ///       and "Self-contained video codec configuration"; the frame is
    ///       appended at a 4 KiB-aligned offset and the reference is written
    ///       after the bytes, so recovery can ignore an unreferenced tail.
    [[nodiscard]] core::Status append_video(std::uint32_t channel_id,
                                            const VideoStreamConfig& config,
                                            const VideoFrameRef& reference,
                                            std::span<const std::byte> frame) noexcept;

    /// @brief Finalize the active segment and open the next one.
    /// @return `Ok`, or a manifest/footer error.
    /// @note Spec `recorder/raw-storage`, requirement "Rotation and retention";
    ///       retention runs between finalizing and opening, and a free-space
    ///       failure halts cleanly without a new segment.
    [[nodiscard]] core::Status rotate() noexcept;

    /// @brief Finalize the active segment and mark the session clean.
    /// @return `Ok`, or a manifest/footer error.
    /// @note Spec `recorder/raw-storage`, requirement "Crash recovery"; a clean
    ///       footer plus `clean_close` distinguishes a closed session from a
    ///       recovered prefix.
    [[nodiscard]] core::Status close() noexcept;

    /// @brief Current manifest snapshot.
    /// @return Manifest with the latest segment catalog.
    [[nodiscard]] const SessionManifest& manifest() const noexcept;

    /// @brief Sequence of the active segment.
    /// @return Segment sequence number.
    [[nodiscard]] std::uint32_t current_segment_seq() const noexcept;

private:
    struct Impl; ///< Hidden buffers, descriptors, and rotation state.
    /// @brief Adopt an already-created implementation.
    /// @param impl Completed implementation state.
    explicit SessionWriter(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_; ///< Owned implementation; empty after a move.
};

} // namespace nexigen::recorder
