// nexigen::recorder — cold-path formatting, JSON, and validation.
#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nexigen/core/integrity/crc32c.hpp>
#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/recorder/channel_declare.hpp>
#include <nexigen/recorder/dataset_info.hpp>
#include <nexigen/recorder/export_selection.hpp>
#include <nexigen/recorder/schema_declare.hpp>
#include <nexigen/recorder/segment_info.hpp>
#include <nexigen/recorder/session_manifest.hpp>
#include <nexigen/recorder/storage_settings.hpp>
#include <nexigen/recorder/wire_types.hpp>

namespace nexigen::recorder {

namespace {

using core::detail::read_bool;
using core::detail::read_string;

template <class T>
void read_uint(const nlohmann::json& object, const char* key, T& out) noexcept {
    if constexpr (std::is_signed_v<T>) {
        static_cast<void>(core::detail::read_signed(object, key, out));
    } else {
        static_cast<void>(core::detail::read_unsigned(object, key, out));
    }
}

template <class Enum>
void read_enum_field(const nlohmann::json& object, const char* key, Enum& out) noexcept {
    if (!object.is_object()) {
        return;
    }
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return;
    }
    Enum parsed = out;
    if (from_string(it->get<std::string>(), parsed)) {
        out = parsed;
    }
}

void read_bytes(const nlohmann::json& object, const char* key, std::vector<std::uint8_t>& out) {
    if (!object.is_object()) {
        return;
    }
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return;
    }
    out.clear();
    for (const auto& element : *it) {
        if (element.is_number_unsigned()) {
            out.push_back(static_cast<std::uint8_t>(element.get<unsigned>()));
        }
    }
}

[[nodiscard]] bool session_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == '.' || c == '/';
}

[[nodiscard]] bool safe_session_id(std::string_view id) noexcept {
    if (id.empty() || id == "." || id == "..") {
        return false;
    }
    if (id.find("..") != std::string_view::npos || id.front() == '/' || id.back() == '/') {
        return false;
    }
    for (const char c : id) {
        if (!session_id_char(c)) {
            return false;
        }
    }
    return true;
}

/// @brief Layout tokens are relative and limited to `{session_id}`/`{label}`.
[[nodiscard]] bool safe_layout(std::string_view layout) noexcept {
    if (layout.empty() || layout.front() == '/' ||
        layout.find("..") != std::string_view::npos) {
        return false;
    }
    for (std::size_t i = 0; i < layout.size(); ++i) {
        if (layout[i] != '{') {
            continue;
        }
        const auto end = layout.find('}', i + 1);
        if (end == std::string_view::npos) {
            return false;
        }
        const std::string_view token(layout.data() + i + 1, end - i - 1);
        if (token != "session_id" && token != "label") {
            return false;
        }
        i = end;
    }
    return true;
}

[[nodiscard]] core::Status validate_schema_list(const std::vector<SchemaDeclare>& schemas) {
    if (schemas.size() > kMaxSchemas) {
        return std::unexpected(core::ErrorCode::ResourceExhausted);
    }
    for (std::size_t i = 0; i < schemas.size(); ++i) {
        if (!schemas[i].validate().has_value()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        for (std::size_t k = i + 1; k < schemas.size(); ++k) {
            if (schemas[i].id == schemas[k].id) {
                return std::unexpected(core::ErrorCode::Conflict);
            }
        }
    }
    return {};
}

[[nodiscard]] bool has_schema(const std::vector<SchemaDeclare>& schemas, std::uint32_t id) {
    for (const auto& schema : schemas) {
        if (schema.id == id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] core::Status validate_channel_list(const std::vector<ChannelDeclare>& channels,
                                                 const std::vector<SchemaDeclare>& schemas) {
    if (channels.size() > kMaxChannels) {
        return std::unexpected(core::ErrorCode::ResourceExhausted);
    }
    for (std::size_t i = 0; i < channels.size(); ++i) {
        if (!channels[i].validate().has_value()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (!has_schema(schemas, channels[i].schema_id)) {
            return std::unexpected(core::ErrorCode::NotFound);
        }
        for (std::size_t k = i + 1; k < channels.size(); ++k) {
            if (channels[i].id == channels[k].id) {
                return std::unexpected(core::ErrorCode::Conflict);
            }
        }
    }
    return {};
}

[[nodiscard]] core::Status validate_segment_list(const std::vector<SegmentInfo>& segments) {
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (segments[i].rec_path.empty() || segments[i].idx_path.empty() ||
            segments[i].vid_path.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        for (std::size_t k = i + 1; k < segments.size(); ++k) {
            if (segments[i].seq == segments[k].seq) {
                return std::unexpected(core::ErrorCode::Conflict);
            }
        }
    }
    return {};
}

} // namespace

// Wire structs ---------------------------------------------------------------

std::string RecordHeader::to_string() const {
    return std::format("RecordHeader(magic=0x{:08X}, type={}, flags={}, channel={}, payload={}, "
                       "commit={}, seq={}, publisher={}, header_crc=0x{:08X}, payload_crc=0x{:08X})",
                       magic, type, flags, channel_id, payload_size, commit_mono_ns, sequence,
                       publisher_id, header_crc32c, payload_crc32c);
}

void to_json(nlohmann::json& j, const RecordHeader& value) {
    j = nlohmann::json{{"magic", value.magic},
                       {"type", value.type},
                       {"flags", value.flags},
                       {"channel_id", value.channel_id},
                       {"payload_size", value.payload_size},
                       {"commit_mono_ns", value.commit_mono_ns},
                       {"sequence", value.sequence},
                       {"publisher_id", value.publisher_id},
                       {"header_crc32c", value.header_crc32c},
                       {"payload_crc32c", value.payload_crc32c}};
}

void from_json(const nlohmann::json& j, RecordHeader& value) {
    read_uint(j, "magic", value.magic);
    read_uint(j, "type", value.type);
    read_uint(j, "flags", value.flags);
    read_uint(j, "channel_id", value.channel_id);
    read_uint(j, "payload_size", value.payload_size);
    read_uint(j, "commit_mono_ns", value.commit_mono_ns);
    read_uint(j, "sequence", value.sequence);
    read_uint(j, "publisher_id", value.publisher_id);
    read_uint(j, "header_crc32c", value.header_crc32c);
    read_uint(j, "payload_crc32c", value.payload_crc32c);
}

std::string IndexEntry::to_string() const {
    return std::format("IndexEntry(commit={}, offset={}, event=[{}, {}], mask=0x{:016X}, "
                       "end={}, records={}, flags={}, crc=0x{:08X})",
                       commit_ns, rec_offset, min_event_ns, max_event_ns, channel_mask,
                       end_rec_offset, record_count, flags, entry_crc32c);
}

void to_json(nlohmann::json& j, const IndexEntry& value) {
    j = nlohmann::json{{"commit_ns", value.commit_ns},
                       {"rec_offset", value.rec_offset},
                       {"min_event_ns", value.min_event_ns},
                       {"max_event_ns", value.max_event_ns},
                       {"channel_mask", value.channel_mask},
                       {"end_rec_offset", value.end_rec_offset},
                       {"record_count", value.record_count},
                       {"flags", value.flags},
                       {"entry_crc32c", value.entry_crc32c}};
}

void from_json(const nlohmann::json& j, IndexEntry& value) {
    read_uint(j, "commit_ns", value.commit_ns);
    read_uint(j, "rec_offset", value.rec_offset);
    read_uint(j, "min_event_ns", value.min_event_ns);
    read_uint(j, "max_event_ns", value.max_event_ns);
    read_uint(j, "channel_mask", value.channel_mask);
    read_uint(j, "end_rec_offset", value.end_rec_offset);
    read_uint(j, "record_count", value.record_count);
    read_uint(j, "flags", value.flags);
    read_uint(j, "entry_crc32c", value.entry_crc32c);
}

std::string VideoFrameRef::to_string() const {
    return std::format("VideoFrameRef(seg={}, offset={}, size={}, crc=0x{:08X}, codec={}, "
                       "bitstream={}, {}x{}, capture={}, source_seq={}, epoch={})",
                       vid_segment_seq, vid_offset, vid_size, vid_crc32c, codec, bitstream, width,
                       height, capture_mono_ns, source_sequence, config_epoch);
}

void to_json(nlohmann::json& j, const VideoFrameRef& value) {
    j = nlohmann::json{{"vid_offset", value.vid_offset},
                       {"capture_mono_ns", value.capture_mono_ns},
                       {"source_sequence", value.source_sequence},
                       {"config_epoch", value.config_epoch},
                       {"vid_size", value.vid_size},
                       {"vid_crc32c", value.vid_crc32c},
                       {"width", value.width},
                       {"height", value.height},
                       {"codec", value.codec},
                       {"bitstream", value.bitstream},
                       {"vid_segment_seq", value.vid_segment_seq}};
}

void from_json(const nlohmann::json& j, VideoFrameRef& value) {
    read_uint(j, "vid_offset", value.vid_offset);
    read_uint(j, "capture_mono_ns", value.capture_mono_ns);
    read_uint(j, "source_sequence", value.source_sequence);
    read_uint(j, "config_epoch", value.config_epoch);
    read_uint(j, "vid_size", value.vid_size);
    read_uint(j, "vid_crc32c", value.vid_crc32c);
    read_uint(j, "width", value.width);
    read_uint(j, "height", value.height);
    read_uint(j, "codec", value.codec);
    read_uint(j, "bitstream", value.bitstream);
    read_uint(j, "vid_segment_seq", value.vid_segment_seq);
}

std::string VideoStreamConfig::to_string() const {
    return std::format("VideoStreamConfig(codec={}, bitstream={}, {}x{}, timebase={}/{}, "
                       "extradata={}, epoch={})",
                       codec, bitstream, width, height, timebase_num, timebase_den, extradata_size,
                       config_epoch);
}

void to_json(nlohmann::json& j, const VideoStreamConfig& value) {
    j = nlohmann::json{{"codec", value.codec},
                       {"bitstream", value.bitstream},
                       {"width", value.width},
                       {"height", value.height},
                       {"timebase_num", value.timebase_num},
                       {"timebase_den", value.timebase_den},
                       {"extradata_size", value.extradata_size},
                       {"config_epoch", value.config_epoch},
                       {"extradata", value.extradata}};
}

void from_json(const nlohmann::json& j, VideoStreamConfig& value) {
    read_uint(j, "codec", value.codec);
    read_uint(j, "bitstream", value.bitstream);
    read_uint(j, "width", value.width);
    read_uint(j, "height", value.height);
    read_uint(j, "timebase_num", value.timebase_num);
    read_uint(j, "timebase_den", value.timebase_den);
    read_uint(j, "extradata_size", value.extradata_size);
    read_uint(j, "config_epoch", value.config_epoch);
    if (j.is_object()) {
        const auto it = j.find("extradata");
        if (it != j.end() && it->is_array()) {
            std::size_t index = 0;
            for (const auto& element : *it) {
                if (index < value.extradata.size() && element.is_number_unsigned()) {
                    value.extradata[index] =
                        static_cast<std::uint8_t>(element.get<unsigned>());
                }
                ++index;
            }
        }
    }
}

std::string GapMarkerPayload::to_string() const {
    return std::format("GapMarker(first_lost={}, lost={}, reason={})", first_lost_sequence,
                       lost_count, reason);
}

void to_json(nlohmann::json& j, const GapMarkerPayload& value) {
    j = nlohmann::json{{"first_lost_sequence", value.first_lost_sequence},
                       {"lost_count", value.lost_count},
                       {"reason", value.reason}};
}

void from_json(const nlohmann::json& j, GapMarkerPayload& value) {
    read_uint(j, "first_lost_sequence", value.first_lost_sequence);
    read_uint(j, "lost_count", value.lost_count);
    read_uint(j, "reason", value.reason);
}

std::string TimeSyncPayload::to_string() const {
    return std::format("TimeSync(mono={}, utc_offset={})", mono_ns, utc_offset_ns);
}

void to_json(nlohmann::json& j, const TimeSyncPayload& value) {
    j = nlohmann::json{{"mono_ns", value.mono_ns}, {"utc_offset_ns", value.utc_offset_ns}};
}

void from_json(const nlohmann::json& j, TimeSyncPayload& value) {
    read_uint(j, "mono_ns", value.mono_ns);
    read_uint(j, "utc_offset_ns", value.utc_offset_ns);
}

std::string SegmentFooter::to_string() const {    return std::format("SegmentFooter(version={}, records={}, gaps={}, rec_bytes={}, vid_bytes={}, "
                       "index_entries={}, end_commit={}, crc=0x{:08X})",
                       version, total_records, total_gaps, rec_bytes, vid_bytes, index_entries,
                       end_commit_ns, footer_crc32c);
}

void to_json(nlohmann::json& j, const SegmentFooter& value) {
    j = nlohmann::json{{"magic", value.magic},
                       {"version", value.version},
                       {"total_records", value.total_records},
                       {"total_gaps", value.total_gaps},
                       {"rec_bytes", value.rec_bytes},
                       {"vid_bytes", value.vid_bytes},
                       {"index_entries", value.index_entries},
                       {"end_commit_ns", value.end_commit_ns},
                       {"footer_crc32c", value.footer_crc32c}};
}

void from_json(const nlohmann::json& j, SegmentFooter& value) {
    read_uint(j, "magic", value.magic);
    read_uint(j, "version", value.version);
    read_uint(j, "total_records", value.total_records);
    read_uint(j, "total_gaps", value.total_gaps);
    read_uint(j, "rec_bytes", value.rec_bytes);
    read_uint(j, "vid_bytes", value.vid_bytes);
    read_uint(j, "index_entries", value.index_entries);
    read_uint(j, "end_commit_ns", value.end_commit_ns);
    read_uint(j, "footer_crc32c", value.footer_crc32c);
}

// Settings -------------------------------------------------------------------

core::Status StorageSettings::validate() const noexcept {
    if (format != RecorderFormat::Raw) {
        return std::unexpected(core::ErrorCode::UnsupportedFormat);
    }
    if (session_root.empty() || session_root.front() != '/') {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!session_id.empty() && !safe_session_id(session_id)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!safe_layout(session_layout)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (max_segment_bytes == 0U || max_segment_duration_ns == 0U ||
        index_stride_bytes == 0U || index_interval_ns == 0U || write_buffer_bytes == 0U ||
        index_buffer_bytes == 0U || video_frame_buffer_bytes == 0U || max_record_bytes == 0U) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

std::string StorageSettings::to_string() const {
    return std::format("StorageSettings(root={}, session={}, layout={}, format={}, domain={}, "
                       "segment_bytes={}, segment_ns={}, segment_records={}, index_stride={}, "
                       "index_interval_ns={}, write_buffer={}, video_buffer={}, video_crc={}, "
                       "retention={})",
                       session_root, session_id, session_layout, recorder::to_string(format),
                       recorder::to_string(time_domain), max_segment_bytes,
                       max_segment_duration_ns, max_segment_records, index_stride_bytes,
                       index_interval_ns, write_buffer_bytes, video_frame_buffer_bytes,
                       recorder::to_string(video_crc), recorder::to_string(retention));
}

void to_json(nlohmann::json& j, const StorageSettings& settings) {
    j = nlohmann::json{{"session_root", settings.session_root},
                       {"session_id", settings.session_id},
                       {"session_layout", settings.session_layout},
                       {"format", settings.format},
                       {"time_domain", settings.time_domain},
                       {"max_segment_bytes", settings.max_segment_bytes},
                       {"max_segment_duration_ns", settings.max_segment_duration_ns},
                       {"max_segment_records", settings.max_segment_records},
                       {"index_stride_bytes", settings.index_stride_bytes},
                       {"index_interval_ns", settings.index_interval_ns},
                       {"write_buffer_bytes", settings.write_buffer_bytes},
                       {"index_buffer_bytes", settings.index_buffer_bytes},
                       {"video_frame_buffer_bytes", settings.video_frame_buffer_bytes},
                       {"max_record_bytes", settings.max_record_bytes},
                       {"retention_max_bytes", settings.retention_max_bytes},
                       {"min_free_bytes", settings.min_free_bytes},
                       {"retention", settings.retention},
                       {"video_crc", settings.video_crc}};
}

void from_json(const nlohmann::json& j, StorageSettings& settings) noexcept {
    read_string(j, "session_root", settings.session_root);
    read_string(j, "session_id", settings.session_id);
    read_string(j, "session_layout", settings.session_layout);
    read_enum_field(j, "format", settings.format);
    read_enum_field(j, "time_domain", settings.time_domain);
    read_uint(j, "max_segment_bytes", settings.max_segment_bytes);
    read_uint(j, "max_segment_duration_ns", settings.max_segment_duration_ns);
    read_uint(j, "max_segment_records", settings.max_segment_records);
    read_uint(j, "index_stride_bytes", settings.index_stride_bytes);
    read_uint(j, "index_interval_ns", settings.index_interval_ns);
    read_uint(j, "write_buffer_bytes", settings.write_buffer_bytes);
    read_uint(j, "index_buffer_bytes", settings.index_buffer_bytes);
    read_uint(j, "video_frame_buffer_bytes", settings.video_frame_buffer_bytes);
    read_uint(j, "max_record_bytes", settings.max_record_bytes);
    read_uint(j, "retention_max_bytes", settings.retention_max_bytes);
    read_uint(j, "min_free_bytes", settings.min_free_bytes);
    read_enum_field(j, "retention", settings.retention);
    read_enum_field(j, "video_crc", settings.video_crc);
}

// Schema / channel / segment / manifest -------------------------------------

core::Status SchemaDeclare::validate() const noexcept {
    if (id == 0U || version == 0U || name.empty() || encoding.empty() ||
        data.size() > kMaxSchemaBytes) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const bool known_encoding = encoding == "jsonschema" || encoding == "protobuf" ||
                                encoding == "flatbuffer" || encoding == "json" ||
                                encoding == "nexigen.pod.v1" || encoding == "ros2msg";
    if (!known_encoding) {
        return std::unexpected(core::ErrorCode::UnsupportedFormat);
    }
    return {};
}

std::string SchemaDeclare::to_string() const {
    return std::format("SchemaDeclare(id={}, name={}, version={}, encoding={}, bytes={}, crc=0x{:08X})",
                       id, name, version, encoding, data.size(), data_crc32c);
}

void to_json(nlohmann::json& j, const SchemaDeclare& schema) {
    j = nlohmann::json{{"id", schema.id},
                       {"name", schema.name},
                       {"version", schema.version},
                       {"encoding", schema.encoding},
                       {"units", schema.units},
                       {"endianness", schema.endianness},
                       {"data", schema.data},
                       {"data_crc32c", schema.data_crc32c}};
}

void from_json(const nlohmann::json& j, SchemaDeclare& schema) noexcept {
    read_uint(j, "id", schema.id);
    read_string(j, "name", schema.name);
    read_uint(j, "version", schema.version);
    read_string(j, "encoding", schema.encoding);
    read_string(j, "units", schema.units);
    read_string(j, "endianness", schema.endianness);
    read_bytes(j, "data", schema.data);
    read_uint(j, "data_crc32c", schema.data_crc32c);
}

core::Status ChannelDeclare::validate() const noexcept {
    if (id == 0U || schema_id == 0U || topic.empty() || topic.size() > 511U ||
        message_encoding.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

std::string ChannelDeclare::to_string() const {
    return std::format("ChannelDeclare(id={}, schema={}, topic={}, kind={}, encoding={})", id,
                       schema_id, topic, recorder::to_string(kind), message_encoding);
}

void to_json(nlohmann::json& j, const ChannelDeclare& channel) {
    j = nlohmann::json{{"id", channel.id},
                       {"schema_id", channel.schema_id},
                       {"topic", channel.topic},
                       {"kind", channel.kind},
                       {"message_encoding", channel.message_encoding},
                       {"metadata", channel.metadata}};
}

void from_json(const nlohmann::json& j, ChannelDeclare& channel) noexcept {
    read_uint(j, "id", channel.id);
    read_uint(j, "schema_id", channel.schema_id);
    read_string(j, "topic", channel.topic);
    read_enum_field(j, "kind", channel.kind);
    read_string(j, "message_encoding", channel.message_encoding);
    read_string(j, "metadata", channel.metadata);
}

std::string SegmentInfo::to_string() const {
    return std::format("SegmentInfo(seq={}, closed={}, records={}, gaps={}, rec_bytes={}, "
                       "vid_bytes={}, commit=[{}, {}])",
                       seq, closed, records, gaps, rec_bytes, vid_bytes, start_commit_ns,
                       end_commit_ns);
}

void to_json(nlohmann::json& j, const SegmentInfo& segment) {
    j = nlohmann::json{{"seq", segment.seq},
                       {"rec_path", segment.rec_path},
                       {"idx_path", segment.idx_path},
                       {"vid_path", segment.vid_path},
                       {"closed", segment.closed},
                       {"records", segment.records},
                       {"gaps", segment.gaps},
                       {"rec_bytes", segment.rec_bytes},
                       {"vid_bytes", segment.vid_bytes},
                       {"start_commit_ns", segment.start_commit_ns},
                       {"end_commit_ns", segment.end_commit_ns}};
}

void from_json(const nlohmann::json& j, SegmentInfo& segment) noexcept {
    read_uint(j, "seq", segment.seq);
    read_string(j, "rec_path", segment.rec_path);
    read_string(j, "idx_path", segment.idx_path);
    read_string(j, "vid_path", segment.vid_path);
    read_bool(j, "closed", segment.closed);
    read_uint(j, "records", segment.records);
    read_uint(j, "gaps", segment.gaps);
    read_uint(j, "rec_bytes", segment.rec_bytes);
    read_uint(j, "vid_bytes", segment.vid_bytes);
    read_uint(j, "start_commit_ns", segment.start_commit_ns);
    read_uint(j, "end_commit_ns", segment.end_commit_ns);
}

core::Status SessionManifest::validate() const noexcept {
    if (format != RecorderFormat::Raw || version == 0U || session_id.empty() ||
        !safe_session_id(session_id)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const auto schemas_status = validate_schema_list(schemas);
    if (!schemas_status.has_value()) {
        return schemas_status;
    }
    const auto channels_status = validate_channel_list(channels, schemas);
    if (!channels_status.has_value()) {
        return channels_status;
    }
    const auto segments_status = validate_segment_list(segments);
    if (!segments_status.has_value()) {
        return segments_status;
    }
    return settings.validate();
}

std::string SessionManifest::to_string() const {
    return std::format("SessionManifest(format={}, version={}, session={}, domain={}, "
                       "schemas={}, channels={}, segments={}, clean={})",
                       recorder::to_string(format), version, session_id,
                       recorder::to_string(time_domain), schemas.size(), channels.size(),
                       segments.size(), clean_close);
}

void to_json(nlohmann::json& j, const SessionManifest& manifest) {
    j = nlohmann::json{{"format", manifest.format},
                       {"version", manifest.version},
                       {"session_id", manifest.session_id},
                       {"time_domain", manifest.time_domain},
                       {"settings", manifest.settings},
                       {"schemas", manifest.schemas},
                       {"channels", manifest.channels},
                       {"segments", manifest.segments},
                       {"clean_close", manifest.clean_close}};
}

void from_json(const nlohmann::json& j, SessionManifest& manifest) noexcept {
    read_enum_field(j, "format", manifest.format);
    read_uint(j, "version", manifest.version);
    read_string(j, "session_id", manifest.session_id);
    read_enum_field(j, "time_domain", manifest.time_domain);
    if (j.is_object()) {
        const auto settings = j.find("settings");
        if (settings != j.end() && settings->is_object()) {
            manifest.settings = settings->get<StorageSettings>();
        }
        const auto read_list = [&](const char* key, auto& target) {
            const auto list = j.find(key);
            if (list == j.end() || !list->is_array()) {
                return;
            }
            target.clear();
            for (const auto& entry : *list) {
                if (entry.is_object()) {
                    using Value = typename std::decay_t<decltype(target)>::value_type;
                    target.push_back(entry.get<Value>());
                }
            }
        };
        read_list("schemas", manifest.schemas);
        read_list("channels", manifest.channels);
        read_list("segments", manifest.segments);
    }
    read_bool(j, "clean_close", manifest.clean_close);
}

// Dataset info ---------------------------------------------------------------

std::string ExportSelection::to_string() const {
    std::string channels_text;
    for (const auto& channel : channels) {
        if (!channels_text.empty()) {
            channels_text += ",";
        }
        channels_text += channel;
    }
    return std::format("ExportSelection(dataset={}, channels=[{}], event=[{}{}, {}{}])",
                       dataset, channels_text, has_start ? "" : "...",
                       has_start ? std::to_string(start_event_ns) : "",
                       has_end ? std::to_string(end_event_ns) : "",
                       has_end ? "" : "...");
}

void to_json(nlohmann::json& j, const ExportSelection& selection) {
    j = nlohmann::json{{"dataset", selection.dataset},
                       {"channels", selection.channels},
                       {"start_event_ns", selection.start_event_ns},
                       {"end_event_ns", selection.end_event_ns},
                       {"has_start", selection.has_start},
                       {"has_end", selection.has_end}};
}

void from_json(const nlohmann::json& j, ExportSelection& selection) noexcept {
    read_string(j, "dataset", selection.dataset);
    read_uint(j, "start_event_ns", selection.start_event_ns);
    read_uint(j, "end_event_ns", selection.end_event_ns);
    read_bool(j, "has_start", selection.has_start);
    read_bool(j, "has_end", selection.has_end);
    if (j.is_object()) {
        const auto list = j.find("channels");
        if (list != j.end() && list->is_array()) {
            selection.channels.clear();
            for (const auto& entry : *list) {
                if (entry.is_string()) {
                    selection.channels.push_back(entry.get<std::string>());
                }
            }
        }
    }
}

std::string DatasetField::to_string() const {
    return std::format("DatasetField(name={}, type={}, units={}, offset={}, element_size={}, "
                       "count={}, stride={})",
                       name, type, units, offset, element_size, count, stride);
}

void to_json(nlohmann::json& j, const DatasetField& field) {
    j = nlohmann::json{{"name", field.name},
                       {"type", field.type},
                       {"units", field.units},
                       {"offset", field.offset},
                       {"element_size", field.element_size},
                       {"count", field.count},
                       {"stride", field.stride}};
}

void from_json(const nlohmann::json& j, DatasetField& field) noexcept {
    read_string(j, "name", field.name);
    read_string(j, "type", field.type);
    read_string(j, "units", field.units);
    read_uint(j, "offset", field.offset);
    read_uint(j, "element_size", field.element_size);
    read_uint(j, "count", field.count);
    read_uint(j, "stride", field.stride);
}

std::string DatasetSegmentInfo::to_string() const {
    return std::format("DatasetSegmentInfo(seq={}, path={}, records={}, gaps={}, bytes={}, "
                       "event=[{}, {}], closed={})",
                       seq, path, records, gaps, bytes, start_event_ns, end_event_ns,
                       closed ? "true" : "false");
}

void to_json(nlohmann::json& j, const DatasetSegmentInfo& segment) {
    j = nlohmann::json{{"seq", segment.seq},
                       {"path", segment.path},
                       {"records", segment.records},
                       {"gaps", segment.gaps},
                       {"bytes", segment.bytes},
                       {"start_event_ns", segment.start_event_ns},
                       {"end_event_ns", segment.end_event_ns},
                       {"closed", segment.closed}};
}

void from_json(const nlohmann::json& j, DatasetSegmentInfo& segment) noexcept {
    read_uint(j, "seq", segment.seq);
    read_string(j, "path", segment.path);
    read_uint(j, "records", segment.records);
    read_uint(j, "gaps", segment.gaps);
    read_uint(j, "bytes", segment.bytes);
    read_uint(j, "start_event_ns", segment.start_event_ns);
    read_uint(j, "end_event_ns", segment.end_event_ns);
    read_bool(j, "closed", segment.closed);
}

std::string DatasetInfo::to_string() const {
    return std::format("DatasetInfo(id={}, name={}, kind={}, schema={}, codec={}, records={}, "
                       "gaps={}, bytes={}, event=[{}, {}], offset=[{}, {}], video={}x{}, "
                       "extradata={}, epochs={}, closed={})",
                       channel_id, name, recorder::to_string(kind), schema_name,
                       recorder::to_string(codec), records, gaps, payload_bytes, first_event_ns,
                       last_event_ns, first_rec_offset, last_rec_offset, width, height,
                       extradata_size, config_epochs.size(),
                       closed ? "true" : "false");
}

void to_json(nlohmann::json& j, const DatasetInfo& dataset) {
    j = nlohmann::json{{"channel_id", dataset.channel_id},
                       {"kind", dataset.kind},
                       {"name", dataset.name},
                       {"schema_id", dataset.schema_id},
                       {"schema_name", dataset.schema_name},
                       {"schema_encoding", dataset.schema_encoding},
                       {"message_encoding", dataset.message_encoding},
                       {"codec", dataset.codec},
                       {"bitstream", dataset.bitstream},
                       {"width", dataset.width},
                       {"height", dataset.height},
                       {"extradata_size", dataset.extradata_size},
                       {"records", dataset.records},
                       {"gaps", dataset.gaps},
                       {"payload_bytes", dataset.payload_bytes},
                       {"first_event_ns", dataset.first_event_ns},
                       {"last_event_ns", dataset.last_event_ns},
                       {"first_rec_offset", dataset.first_rec_offset},
                       {"last_rec_offset", dataset.last_rec_offset},
                       {"closed", dataset.closed},
                       {"config_epochs", dataset.config_epochs},
                       {"fields", dataset.fields},
                       {"segments", dataset.segments}};
}

void from_json(const nlohmann::json& j, DatasetInfo& dataset) noexcept {
    read_uint(j, "channel_id", dataset.channel_id);
    read_enum_field(j, "kind", dataset.kind);
    read_string(j, "name", dataset.name);
    read_uint(j, "schema_id", dataset.schema_id);
    read_string(j, "schema_name", dataset.schema_name);
    read_string(j, "schema_encoding", dataset.schema_encoding);
    read_string(j, "message_encoding", dataset.message_encoding);
    read_enum_field(j, "codec", dataset.codec);
    read_enum_field(j, "bitstream", dataset.bitstream);
    read_uint(j, "width", dataset.width);
    read_uint(j, "height", dataset.height);
    read_uint(j, "extradata_size", dataset.extradata_size);
    read_uint(j, "records", dataset.records);
    read_uint(j, "gaps", dataset.gaps);
    read_uint(j, "payload_bytes", dataset.payload_bytes);
    read_uint(j, "first_event_ns", dataset.first_event_ns);
    read_uint(j, "last_event_ns", dataset.last_event_ns);
    read_uint(j, "first_rec_offset", dataset.first_rec_offset);
    read_uint(j, "last_rec_offset", dataset.last_rec_offset);
    read_bool(j, "closed", dataset.closed);
    if (j.is_object()) {
        if (const auto epochs = j.find("config_epochs");
            epochs != j.end() && epochs->is_array()) {
            dataset.config_epochs.clear();
            for (const auto& entry : *epochs) {
                if (entry.is_number_unsigned()) {
                    dataset.config_epochs.push_back(entry.get<std::uint64_t>());
                }
            }
        }
        const auto read_list = [&](const char* key, auto& target) {
            const auto list = j.find(key);
            if (list == j.end() || !list->is_array()) {
                return;
            }
            target.clear();
            for (const auto& entry : *list) {
                if (entry.is_object()) {
                    using Value = typename std::decay_t<decltype(target)>::value_type;
                    target.push_back(entry.get<Value>());
                }
            }
        };
        read_list("fields", dataset.fields);
        read_list("segments", dataset.segments);
    }
}

} // namespace nexigen::recorder
