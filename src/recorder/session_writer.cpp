// nexigen::recorder — preallocated raw session writer.
#include <nexigen/recorder/session_writer.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/timestamp.hpp>

#include "crc32c_fast.hpp"

namespace nexigen::recorder {

namespace {

[[nodiscard]] bool write_all(int fd, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t written = ::write(fd, bytes + offset, size - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

[[nodiscard]] std::uint64_t mono_now() noexcept {
    const auto now = chrono::Timestamp::now();
    return now.has_value() ? now->nanoseconds() : 0ULL;
}

[[nodiscard]] std::string segment_base(const std::string& session_id, std::uint32_t seq) {
    return std::format("{}_{:06}", session_id, seq);
}

/// @brief Bit `channel_id` for `IndexEntry.channel_mask`; ids 0 and 64+ have no bit.
[[nodiscard]] constexpr std::uint64_t channel_bit(std::uint32_t channel_id) noexcept {
    return channel_id > 0U && channel_id < 64U ? (1ULL << channel_id) : 0ULL;
}

} // namespace

struct SessionWriter::Impl {
    StorageSettings settings{};
    SessionManifest manifest{};
    std::string session_dir{};
    int rec_fd{-1};
    int idx_fd{-1};
    int vid_fd{-1};
    std::vector<std::byte> buffer{};
    std::size_t used{0};
    std::uint64_t rec_stream_offset{0};
    std::uint64_t vid_stream_offset{0};
    std::uint64_t segment_start_commit{0};
    std::uint64_t segment_end_commit{0};
    std::uint64_t segment_records{0};
    std::uint64_t segment_gaps{0};
    std::uint64_t index_entries{0};
    std::uint64_t interval_start_offset{0};
    std::uint64_t interval_start_commit{0};
    std::uint64_t interval_min_event{0};
    std::uint64_t interval_max_event{0};
    std::uint32_t interval_records{0};
    std::uint64_t interval_channel_mask{0};
    std::array<std::uint32_t, kMaxChannels> channel_schema{};
    std::array<DatasetKind, kMaxChannels> channel_kind{};
    std::array<bool, kMaxChannels> channel_declared{};
    std::array<std::uint64_t, kMaxChannels> channel_config_epoch{};
    std::array<std::uint64_t, kMaxChannels> channel_last_sequence{};
    std::array<bool, kMaxChannels> channel_sequence_valid{};
    bool closed{false};
    bool halted{false};

    [[nodiscard]] bool write_manifest() noexcept {
        const std::string path = session_dir + "/session.json";
        const std::string tmp = path + ".tmp";
        const int fd = ::open(tmp.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd < 0) {
            return false;
        }
        const std::string text = nlohmann::json(manifest).dump(2);
        const bool ok = write_all(fd, text.data(), text.size()) && ::fsync(fd) == 0;
        static_cast<void>(::close(fd));
        if (!ok) {
            return false;
        }
        if (::rename(tmp.c_str(), path.c_str()) != 0) {
            return false;
        }
        const int dir_fd = ::open(session_dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir_fd < 0) {
            return false;
        }
        const bool synced = ::fsync(dir_fd) == 0;
        static_cast<void>(::close(dir_fd));
        return synced;
    }

    [[nodiscard]] bool flush_buffer() noexcept {
        if (used == 0U) {
            return true;
        }
        if (!write_all(rec_fd, buffer.data(), used)) {
            return false;
        }
        used = 0;
        return true;
    }

    [[nodiscard]] bool append_raw(RecordType type, std::uint32_t channel_id,
                                  std::span<const std::byte> payload, std::uint64_t event_ns,
                                  std::uint64_t sequence, std::uint64_t publisher_id,
                                  bool track_index) noexcept {
        const std::uint64_t record_offset = rec_stream_offset;
        RecordHeader header{};
        header.type = static_cast<std::uint16_t>(type);
        header.channel_id = channel_id;
        header.payload_size = static_cast<std::uint32_t>(payload.size());
        header.commit_mono_ns = mono_now();
        header.sequence = sequence;
        header.publisher_id = publisher_id;
        header.header_crc32c =
            detail::crc32c(&header, offsetof(RecordHeader, header_crc32c));
        header.payload_crc32c = detail::crc32c(payload.data(), payload.size());

        if (track_index) {
            if (interval_records == 0U) {
                interval_start_offset = record_offset;
                interval_start_commit = header.commit_mono_ns;
                interval_min_event = event_ns;
                interval_max_event = event_ns;
                interval_channel_mask = channel_bit(channel_id);
            } else {
                interval_min_event = std::min(interval_min_event, event_ns);
                interval_max_event = std::max(interval_max_event, event_ns);
                interval_channel_mask |= channel_bit(channel_id);
            }
            ++interval_records;
        }
        if (segment_records == 0U) {
            segment_start_commit = header.commit_mono_ns;
        }
        segment_end_commit = header.commit_mono_ns;
        ++segment_records;
        if (type == RecordType::GapMarker) {
            ++segment_gaps;
        }

        const std::size_t total = sizeof(header) + payload.size();
        if (total > buffer.size() - used) {
            if (!flush_buffer()) {
                return false;
            }
            if (total > buffer.size()) {
                if (!write_all(rec_fd, &header, sizeof(header)) ||
                    !write_all(rec_fd, payload.data(), payload.size())) {
                    return false;
                }
                rec_stream_offset += total;
                return true;
            }
        }
        std::memcpy(buffer.data() + used, &header, sizeof(header));
        used += sizeof(header);
        if (!payload.empty()) {
            std::memcpy(buffer.data() + used, payload.data(), payload.size());
            used += payload.size();
        }
        rec_stream_offset += total;
        return true;
    }

    [[nodiscard]] bool emit_index() noexcept {
        if (interval_records == 0U) {
            return true;
        }
        IndexEntry entry{};
        entry.commit_ns = interval_start_commit;
        entry.rec_offset = interval_start_offset;
        entry.min_event_ns = interval_min_event;
        entry.max_event_ns = interval_max_event;
        entry.channel_mask = interval_channel_mask;
        entry.end_rec_offset = rec_stream_offset;
        entry.record_count = interval_records;
        entry.entry_crc32c = detail::crc32c(&entry, offsetof(IndexEntry, entry_crc32c));
        const bool ok = write_all(idx_fd, &entry, sizeof(entry));
        if (ok) {
            ++index_entries;
            interval_records = 0;
            interval_channel_mask = 0;
        }
        return ok;
    }

    [[nodiscard]] bool maybe_emit_index(std::uint64_t commit_ns) noexcept {
        if (interval_records == 0U) {
            return true;
        }
        const std::uint64_t bytes = rec_stream_offset - interval_start_offset;
        if (bytes >= settings.index_stride_bytes ||
            (commit_ns >= interval_start_commit &&
             commit_ns - interval_start_commit >= settings.index_interval_ns)) {
            return emit_index();
        }
        return true;
    }

    [[nodiscard]] bool write_footer() noexcept {
        if (!flush_buffer() || !emit_index()) {
            return false;
        }
        SegmentFooter footer{};
        footer.total_records = segment_records;
        footer.total_gaps = segment_gaps;
        footer.rec_bytes = rec_stream_offset;
        footer.vid_bytes = vid_stream_offset;
        footer.index_entries = index_entries;
        footer.end_commit_ns = segment_end_commit;
        footer.footer_crc32c = detail::crc32c(&footer, offsetof(SegmentFooter, footer_crc32c));
        return write_all(rec_fd, &footer, sizeof(footer));
    }

    [[nodiscard]] std::uint64_t closed_bytes() const noexcept {
        std::uint64_t total = 0;
        for (const auto& segment : manifest.segments) {
            if (segment.closed) {
                total += segment.rec_bytes + segment.vid_bytes;
            }
        }
        return total;
    }

    /// @brief Bytes accounted against `max_segment_bytes` (D10: rec + vid).
    ///
    /// The index file is sparse bookkeeping and is not part of the segment
    /// payload budget. Both streams reset in `open_segment()`.
    [[nodiscard]] std::uint64_t segment_bytes() const noexcept {
        return rec_stream_offset + vid_stream_offset;
    }

    void enforce_retention() noexcept {
        if (settings.retention_max_bytes == 0U) {
            return;
        }
        if (settings.retention == RetentionPolicy::Stop) {
            if (closed_bytes() > settings.retention_max_bytes) {
                halted = true;
            }
            return;
        }
        while (closed_bytes() > settings.retention_max_bytes &&
               manifest.segments.size() > 1U) {
            auto victim = manifest.segments.begin();
            while (victim != manifest.segments.end() &&
                   (victim->closed == false || victim + 1 == manifest.segments.end())) {
                ++victim;
            }
            if (victim == manifest.segments.end()) {
                break;
            }
            std::error_code error;
            std::filesystem::remove(session_dir + "/" + victim->rec_path, error);
            std::filesystem::remove(session_dir + "/" + victim->idx_path, error);
            std::filesystem::remove(session_dir + "/" + victim->vid_path, error);
            manifest.segments.erase(victim);
        }
    }

    [[nodiscard]] bool maybe_emit_gap(std::uint32_t channel_id,
                                      const VideoFrameRef& reference) noexcept {
        if (!channel_sequence_valid[channel_id] ||
            reference.source_sequence <= channel_last_sequence[channel_id] + 1U) {
            channel_last_sequence[channel_id] = reference.source_sequence;
            channel_sequence_valid[channel_id] = true;
            return true;
        }
        GapMarkerPayload gap{};
        gap.first_lost_sequence = channel_last_sequence[channel_id] + 1U;
        gap.lost_count = reference.source_sequence - gap.first_lost_sequence;
        gap.reason = static_cast<std::uint16_t>(GapReason::StreamDiscontinuity);
        const auto gap_bytes = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(&gap), sizeof(gap));
        const bool ok = append_raw(RecordType::GapMarker, channel_id, gap_bytes,
                                   reference.capture_mono_ns, reference.source_sequence, 0, true);
        channel_last_sequence[channel_id] = reference.source_sequence;
        channel_sequence_valid[channel_id] = true;
        return ok;
    }

    [[nodiscard]] bool free_space_ok() noexcept {
        if (settings.min_free_bytes == 0U) {
            return true;
        }
        std::error_code error;
        const auto info = std::filesystem::space(session_dir, error);
        return !error && info.available >= settings.min_free_bytes;
    }

    [[nodiscard]] bool write_declarations() noexcept {
        for (const auto& schema : manifest.schemas) {
            nlohmann::json j = schema;
            const std::string text = j.dump();
            const auto bytes = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(text.data()), text.size());
            if (!append_raw(RecordType::SchemaDeclare, 0, bytes, 0, 0, 0, false)) {
                return false;
            }
        }
        for (const auto& channel : manifest.channels) {
            nlohmann::json j = channel;
            const std::string text = j.dump();
            const auto bytes = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(text.data()), text.size());
            if (!append_raw(RecordType::ChannelDeclare, 0, bytes, 0, 0, 0, false)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool open_segment(std::uint32_t seq) noexcept {
        const std::string base = segment_base(settings.session_id, seq);
        const std::string rec = session_dir + "/" + base + ".nxrec";
        const std::string idx = session_dir + "/" + base + ".nxidx";
        const std::string vid = session_dir + "/" + base + ".nxvid";
        rec_fd = ::open(rec.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        idx_fd = ::open(idx.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        vid_fd = ::open(vid.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (rec_fd < 0 || idx_fd < 0 || vid_fd < 0) {
            return false;
        }
        SegmentInfo info{};
        info.seq = seq;
        info.rec_path = base + ".nxrec";
        info.idx_path = base + ".nxidx";
        info.vid_path = base + ".nxvid";
        manifest.segments.push_back(info);
        used = 0;
        rec_stream_offset = 0;
        vid_stream_offset = 0;
        segment_start_commit = 0;
        segment_end_commit = 0;
        segment_records = 0;
        segment_gaps = 0;
        index_entries = 0;
        interval_records = 0;
        interval_channel_mask = 0;
        channel_config_epoch.fill(std::numeric_limits<std::uint64_t>::max());
        return write_declarations();
    }

    void close_fds() noexcept {
        if (rec_fd >= 0) {
            static_cast<void>(::fsync(rec_fd));
            static_cast<void>(::close(rec_fd));
            rec_fd = -1;
        }
        if (idx_fd >= 0) {
            static_cast<void>(::fsync(idx_fd));
            static_cast<void>(::close(idx_fd));
            idx_fd = -1;
        }
        if (vid_fd >= 0) {
            static_cast<void>(::fsync(vid_fd));
            static_cast<void>(::close(vid_fd));
            vid_fd = -1;
        }
    }

    [[nodiscard]] bool finalize_segment() noexcept {
        if (rec_fd < 0) {
            return true;
        }
        const bool ok = write_footer();
        close_fds();
        for (auto& segment : manifest.segments) {
            if (&segment == &manifest.segments.back()) {
                segment.closed = ok;
                segment.records = segment_records;
                segment.gaps = segment_gaps;
                segment.rec_bytes = rec_stream_offset;
                segment.vid_bytes = vid_stream_offset;
                segment.start_commit_ns = segment_start_commit;
                segment.end_commit_ns = segment_end_commit;
            }
        }
        return ok;
    }
};

SessionWriter::SessionWriter(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
SessionWriter::SessionWriter(SessionWriter&&) noexcept = default;
SessionWriter& SessionWriter::operator=(SessionWriter&&) noexcept = default;

SessionWriter::~SessionWriter() {
    if (impl_ && !impl_->closed) {
        static_cast<void>(close());
    }
}

core::Result<SessionWriter> SessionWriter::create(const StorageSettings& settings) noexcept {
    if (!settings.validate().has_value()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    auto impl = std::make_unique<Impl>();
    impl->settings = settings;
    impl->manifest = SessionManifest{};
    impl->manifest.format = RecorderFormat::Raw;
    impl->manifest.version = kFormatVersion;
    impl->manifest.session_id = settings.session_id;
    if (impl->manifest.session_id.empty()) {
        impl->manifest.session_id = std::format("session-{}", mono_now());
    }
    impl->manifest.time_domain = settings.time_domain;
    impl->manifest.settings = settings;
    impl->manifest.settings.session_id = impl->manifest.session_id;
    std::string layout = settings.session_layout;
    const std::string token = "{session_id}";
    for (std::size_t pos = layout.find(token); pos != std::string::npos;
         pos = layout.find(token, pos + impl->manifest.session_id.size())) {
        layout.replace(pos, token.size(), impl->manifest.session_id);
    }
    const std::string label_token = "{label}";
    for (std::size_t pos = layout.find(label_token); pos != std::string::npos;
         pos = layout.find(label_token, pos + impl->manifest.session_id.size())) {
        layout.replace(pos, label_token.size(), impl->manifest.session_id);
    }
    impl->session_dir = settings.session_root + "/" + layout;
    std::error_code error;
    std::filesystem::create_directories(impl->session_dir, error);
    if (error) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    impl->buffer.resize(static_cast<std::size_t>(settings.write_buffer_bytes));
    if (!impl->open_segment(0U) || !impl->write_manifest()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return SessionWriter{std::move(impl)};
}

core::Status SessionWriter::declare_schema(const SchemaDeclare& schema) noexcept {
    if (!impl_ || !schema.validate().has_value() || impl_->manifest.schemas.size() >= kMaxSchemas) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (const auto& existing : impl_->manifest.schemas) {
        if (existing.id == schema.id) {
            return std::unexpected(core::ErrorCode::Conflict);
        }
    }
    SchemaDeclare stored = schema;
    if (stored.data_crc32c == 0U) {
        stored.data_crc32c = detail::crc32c(stored.data.data(), stored.data.size());
    }
    impl_->manifest.schemas.push_back(stored);
    nlohmann::json j = stored;
    const std::string text = j.dump();
    const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
    if (!impl_->append_raw(RecordType::SchemaDeclare, 0, bytes, 0, 0, 0, false)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (!impl_->write_manifest()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

core::Result<std::uint32_t>
SessionWriter::declare_channel(std::string_view topic, std::uint32_t schema_id, DatasetKind kind,
                               std::string_view message_encoding,
                               std::string_view metadata) noexcept {
    if (!impl_) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    for (const auto& existing : impl_->manifest.channels) {
        if (existing.topic == topic && existing.schema_id == schema_id) {
            return existing.id;
        }
    }
    for (std::uint32_t id = 1; id < kMaxChannels; ++id) {
        bool used = false;
        for (const auto& existing : impl_->manifest.channels) {
            if (existing.id == id) {
                used = true;
                break;
            }
        }
        if (used) {
            continue;
        }
        ChannelDeclare channel{};
        channel.id = id;
        channel.schema_id = schema_id;
        channel.topic = std::string(topic);
        channel.kind = kind;
        channel.message_encoding = std::string(message_encoding);
        channel.metadata = std::string(metadata);
        const auto status = declare_channel(channel);
        if (!status.has_value()) {
            return std::unexpected(status.error());
        }
        return id;
    }
    return std::unexpected(core::ErrorCode::ResourceExhausted);
}

core::Status SessionWriter::declare_channel(const ChannelDeclare& channel) noexcept {
    if (!impl_ || !channel.validate().has_value() ||
        impl_->manifest.channels.size() >= kMaxChannels || channel.id >= kMaxChannels) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    bool schema_found = false;
    for (const auto& schema : impl_->manifest.schemas) {
        if (schema.id == channel.schema_id) {
            schema_found = true;
            break;
        }
    }
    if (!schema_found) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    for (const auto& existing : impl_->manifest.channels) {
        if (existing.id == channel.id) {
            return std::unexpected(core::ErrorCode::Conflict);
        }
    }
    impl_->manifest.channels.push_back(channel);
    impl_->channel_schema[channel.id] = channel.schema_id;
    impl_->channel_kind[channel.id] = channel.kind;
    impl_->channel_declared[channel.id] = true;
    nlohmann::json j = channel;
    const std::string text = j.dump();
    const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
    if (!impl_->append_raw(RecordType::ChannelDeclare, 0, bytes, 0, 0, 0, false)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (!impl_->write_manifest()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

core::Status SessionWriter::append(RecordType type, std::uint32_t channel_id,
                                   std::span<const std::byte> payload, std::uint64_t event_ns,
                                   std::uint64_t sequence,
                                   std::uint64_t publisher_id) noexcept {
    if (!impl_ || impl_->closed || impl_->halted || channel_id >= kMaxChannels ||
        !impl_->channel_declared[channel_id] ||
        payload.size() > impl_->settings.max_record_bytes) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!impl_->append_raw(type, channel_id, payload, event_ns, sequence, publisher_id, true)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (!impl_->maybe_emit_index(mono_now())) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (impl_->segment_bytes() >= impl_->settings.max_segment_bytes ||
        (impl_->settings.max_segment_records > 0U &&
         impl_->segment_records >= impl_->settings.max_segment_records) ||
        (impl_->segment_end_commit >= impl_->segment_start_commit &&
         impl_->segment_end_commit - impl_->segment_start_commit >=
             impl_->settings.max_segment_duration_ns)) {
        return rotate();
    }
    return {};
}

core::Status SessionWriter::append_from_shard(RecordShard& shard) noexcept {
    if (!impl_ || impl_->closed || impl_->halted) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    RecordEnvelope envelope{};
    while (shard.try_pop(envelope).has_value()) {
        if (envelope.payload_size > envelope.payload.size()) {
            return std::unexpected(core::ErrorCode::CorruptData);
        }
        const auto payload = std::span<const std::byte>(envelope.payload.data(),
                                                        envelope.payload_size);
        const auto status = append(envelope.type, envelope.channel_id, payload, envelope.event_ns,
                                   envelope.sequence, envelope.publisher_id);
        if (!status.has_value()) {
            return status;
        }
    }
    const auto dropped = shard.take_dropped();
    if (dropped.has_value()) {
        GapMarkerPayload gap{};
        gap.first_lost_sequence = dropped->first_sequence;
        gap.lost_count = dropped->count;
        gap.reason = static_cast<std::uint16_t>(GapReason::IoBackpressure);
        const auto gap_bytes = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(&gap), sizeof(gap));
        if (!impl_->append_raw(RecordType::GapMarker, dropped->channel_id, gap_bytes,
                               mono_now(), dropped->first_sequence, 0, true)) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (!impl_->maybe_emit_index(mono_now())) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
    }
    return {};
}

core::Status SessionWriter::append_time_sync(std::int64_t utc_offset_ns) noexcept {
    if (!impl_ || impl_->closed || impl_->halted) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    TimeSyncPayload sync{};
    sync.mono_ns = mono_now();
    sync.utc_offset_ns = utc_offset_ns;
    const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(&sync), sizeof(sync));
    if (!impl_->append_raw(RecordType::TimeSync, 0, bytes, sync.mono_ns, 0, 0, true)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (!impl_->maybe_emit_index(mono_now())) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

core::Status SessionWriter::append_video(std::uint32_t channel_id,
                                         const VideoStreamConfig& config,
                                         const VideoFrameRef& reference,
                                         std::span<const std::byte> frame) noexcept {
    if (!impl_ || impl_->closed || channel_id >= kMaxChannels ||
        !impl_->channel_declared[channel_id] ||
        impl_->channel_kind[channel_id] != DatasetKind::Video ||
        frame.size() > impl_->settings.video_frame_buffer_bytes) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const auto config_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(&config), sizeof(config));
    if (impl_->channel_config_epoch[channel_id] != config.config_epoch) {
        if (!impl_->append_raw(RecordType::VideoStreamConfig, channel_id, config_bytes,
                               reference.capture_mono_ns, 0, 0, false)) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        impl_->channel_config_epoch[channel_id] = config.config_epoch;
        impl_->channel_sequence_valid[channel_id] = false;
    }

    if (!impl_->maybe_emit_gap(channel_id, reference)) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    const std::uint64_t current = impl_->vid_stream_offset;
    const std::size_t remainder = current % kVideoAlignment;
    const std::size_t padding = remainder == 0U ? 0U : kVideoAlignment - remainder;
    static const std::array<std::byte, kVideoAlignment> zeros{};
    if (!write_all(impl_->vid_fd, zeros.data(), padding) ||
        !write_all(impl_->vid_fd, frame.data(), frame.size())) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    impl_->vid_stream_offset = current + padding + frame.size();

    VideoFrameRef stored = reference;
    stored.vid_offset = current + padding;
    stored.vid_size = static_cast<std::uint32_t>(frame.size());
    // The reference names the segment's manifest sequence, not its position in
    // `manifest.segments`: circular retention erases earlier entries, so the
    // two diverge as soon as a segment is deleted.
    stored.vid_segment_seq = static_cast<std::uint32_t>(impl_->manifest.segments.back().seq);
    if (impl_->settings.video_crc == VideoCrcMode::PerFrame) {
        stored.vid_crc32c = detail::crc32c(frame.data(), frame.size());
    } else {
        stored.vid_crc32c = 0;
    }
    const auto ref_bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(&stored), sizeof(stored));
    return append(RecordType::VideoFrameRef, channel_id, ref_bytes, stored.capture_mono_ns,
                  stored.source_sequence, 0);
}

core::Status SessionWriter::rotate() noexcept {
    if (!impl_ || impl_->closed || impl_->halted) {
        return std::unexpected(core::ErrorCode::NotInitialized);
    }
    if (!impl_->finalize_segment()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    const std::uint32_t next =
        impl_->manifest.segments.empty() ? 0U : impl_->manifest.segments.back().seq + 1U;
    impl_->enforce_retention();
    if (impl_->halted) {
        impl_->manifest.clean_close = true;
        static_cast<void>(impl_->write_manifest());
        return std::unexpected(core::ErrorCode::ResourceExhausted);
    }
    if (!impl_->free_space_ok()) {
        impl_->halted = true;
        impl_->manifest.clean_close = true;
        static_cast<void>(impl_->write_manifest());
        return std::unexpected(core::ErrorCode::ResourceExhausted);
    }
    if (!impl_->open_segment(next) || !impl_->write_manifest()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

core::Status SessionWriter::close() noexcept {
    if (!impl_ || impl_->closed) {
        return {};
    }
    const bool ok = impl_->finalize_segment();
    impl_->enforce_retention();
    impl_->manifest.clean_close = ok;
    if (!impl_->write_manifest()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    impl_->closed = true;
    return ok ? core::Status{} : std::unexpected(core::ErrorCode::SystemError);
}

const SessionManifest& SessionWriter::manifest() const noexcept {
    return impl_->manifest;
}

std::uint32_t SessionWriter::current_segment_seq() const noexcept {
    return static_cast<std::uint32_t>(impl_->manifest.segments.size() - 1U);
}

} // namespace nexigen::recorder
