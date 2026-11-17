// nexigen::recorder — raw session reader shared by the converter.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/recorder/dataset_info.hpp>
#include <nexigen/recorder/session_manifest.hpp>
#include <nexigen/recorder/wire_types.hpp>

namespace nexigen::recorder {

/// @brief Callback receiving one validated record payload.
/// @param context Opaque caller state passed through unchanged.
/// @param header Validated record header.
/// @param payload Record payload bytes.
/// @param offset Byte offset of the record inside its `.nxrec`.
/// @return `true` to continue visiting, `false` to stop successfully.
using RecordVisitor = bool (*)(void* context, const RecordHeader& header,
                               std::span<const std::byte> payload, std::uint64_t offset);

/// @brief Location of the first integrity/structural failure found by `verify`.
struct VerifyFailure {
    std::uint32_t segment_seq{0}; ///< segment sequence containing the failure
    std::uint64_t offset{0};      ///< byte offset inside the segment file
    std::uint32_t channel_id{0};  ///< channel of the failing record, 0 if not a record
};

/// @brief One publisher epoch: a publisher id with a monotonically increasing sequence.
struct PublisherEpoch {
    std::uint32_t channel_id{0};  ///< channel the epoch belongs to
    std::uint64_t publisher_id{0};///< publisher identity for the epoch
    std::uint64_t first_sequence{0}; ///< first sequence observed in the epoch
    std::uint64_t last_sequence{0};  ///< last sequence observed in the epoch
    std::uint64_t records{0};     ///< records observed in the epoch
};

/// @brief Read-only view of one raw session.
class SessionReader {
public:
    /// @brief Open a session directory and parse `session.json`.
    /// @param session_directory Directory containing `session.json` and the segments.
    /// @return Reader, or `NotFound`/`InvalidArgument` when the manifest is absent or invalid.
    [[nodiscard]] static core::Result<SessionReader>
    open(const std::string& session_directory) noexcept;

    /// @brief Open one bare `.nxrec` using only its leading declarations.
    ///
    /// The segment must start with `SchemaDeclare`/`ChannelDeclare` records.
    /// `.nxidx` and `.nxvid` are derived from the record file name; a missing
    /// index is rebuilt on demand for ranged queries.
    /// @param rec_path Path to the `.nxrec` file.
    /// @return Reader over the single segment, or `NotFound`/`InvalidArgument`.
    [[nodiscard]] static core::Result<SessionReader>
    open_segment(const std::string& rec_path) noexcept;

    SessionReader(const SessionReader&) = delete;
    SessionReader& operator=(const SessionReader&) = delete;
    SessionReader(SessionReader&&) noexcept = default;
    SessionReader& operator=(SessionReader&&) noexcept = default;
    ~SessionReader() = default;

    [[nodiscard]] const SessionManifest& manifest() const noexcept { return manifest_; }
    [[nodiscard]] const std::string& directory() const noexcept { return directory_; }

    /// @brief Build the inventory from declarations and record headers.
    /// @return One `DatasetInfo` per declared channel, sorted by channel id.
    [[nodiscard]] core::Result<std::vector<DatasetInfo>> datasets() const noexcept;

    /// @brief Visit records, optionally filtered by channel and event range.
    /// @param channel_id Channel to keep when `has_channel` is true.
    /// @param has_channel Enables the channel filter.
    /// @param start_event_ns Inclusive start of the event-time window.
    /// @param end_event_ns Inclusive end of the event-time window.
    /// @param has_range Enables the event-time window.
    /// @param visitor Callback invoked for every matching record.
    /// @param context Opaque state passed to `visitor`.
    /// @return `Ok`, or a failure when a record fails validation.
    [[nodiscard]] core::Status for_each(std::uint32_t channel_id, bool has_channel,
                                        std::uint64_t start_event_ns, std::uint64_t end_event_ns,
                                        bool has_range, RecordVisitor visitor,
                                        void* context) const noexcept;

    /// @brief Read one video frame's bytes and verify its optional CRC.
    /// @param reference Reference emitted by the writer.
    /// @return Frame bytes, or `NotFound`/`CorruptData`/`SecurityError`.
    [[nodiscard]] core::Result<std::vector<std::byte>>
    read_video(const VideoFrameRef& reference) const noexcept;

    /// @brief Rebuild a segment's sparse index from `.nxrec` in memory.
    /// @param segment_seq Segment sequence number.
    /// @return Rebuilt index entries, or `NotFound`.
    [[nodiscard]] core::Result<std::vector<IndexEntry>>
    recover_index(std::uint32_t segment_seq) const noexcept;

    /// @brief Split records into publisher epochs per channel.
    /// @return Epochs in first-observation order, or a scan failure.
    [[nodiscard]] core::Result<std::vector<PublisherEpoch>>
    publisher_epochs() const noexcept;

    /// @brief Verify record CRCs and count records/gaps.
    /// @param records Receives the validated record count.
    /// @param gaps Receives the gap-marker count.
    /// @param failure Optional location of the first failure.
    /// @return `Ok` when every record/footer matches, otherwise the failure code.
    [[nodiscard]] core::Status verify(std::uint64_t& records, std::uint64_t& gaps,
                                      VerifyFailure* failure = nullptr) const noexcept;

    /// @brief The absolute `.nxrec` path of a segment.
    /// @param segment_seq Segment sequence number.
    /// @return Path, or an empty string when the sequence is unknown.
    [[nodiscard]] std::string rec_path(std::uint32_t segment_seq) const;
    /// @brief The absolute `.nxvid` path of a segment.
    /// @param segment_seq Segment sequence number.
    /// @return Path, or an empty string when the sequence is unknown.
    [[nodiscard]] std::string vid_path(std::uint32_t segment_seq) const;

private:
    SessionReader(std::string directory, SessionManifest manifest) noexcept;

    std::string directory_;
    SessionManifest manifest_;
};

} // namespace nexigen::recorder
