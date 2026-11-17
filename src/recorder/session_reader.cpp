// nexigen::recorder — raw session reader and dataset inventory.
#include <nexigen/recorder/session_reader.hpp>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/detail/json_field.hpp>

#include "crc32c_fast.hpp"

namespace nexigen::recorder {

namespace {

constexpr std::uint32_t kMaxReaderPayload = 64U * 1024U * 1024U;

[[nodiscard]] constexpr std::uint64_t channel_bit(std::uint32_t channel_id) noexcept {
    return channel_id > 0U && channel_id < 64U ? (1ULL << channel_id) : 0ULL;
}

[[nodiscard]] bool read_exact(std::ifstream& input, void* data, std::size_t size) {
    input.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    return input.gcount() == static_cast<std::streamsize>(size);
}

[[nodiscard]] bool read_header(std::ifstream& input, RecordHeader& header) {
    return read_exact(input, &header, sizeof(header));
}

[[nodiscard]] bool valid_header(const RecordHeader& header) {
    return header.magic == kRecordMagic &&
           header.header_crc32c ==
               detail::crc32c(&header, offsetof(RecordHeader, header_crc32c));
}

[[nodiscard]] std::uint64_t event_of(const RecordHeader& header,
                                     std::span<const std::byte> payload) noexcept {
    if (header.type == static_cast<std::uint16_t>(RecordType::VideoFrameRef) &&
        payload.size() == sizeof(VideoFrameRef)) {
        VideoFrameRef reference{};
        std::memcpy(&reference, payload.data(), sizeof(reference));
        return reference.capture_mono_ns;
    }
    return header.commit_mono_ns;
}

[[nodiscard]] std::vector<IndexEntry> read_index(const std::string& path) {
    std::vector<IndexEntry> entries;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return entries;
    }
    IndexEntry entry{};
    while (input.read(reinterpret_cast<char*>(&entry), sizeof(entry))) {
        if (entry.entry_crc32c !=
            detail::crc32c(&entry, offsetof(IndexEntry, entry_crc32c))) {
            break;
        }
        entries.push_back(entry);
    }
    return entries;
}

/// @brief Rebuild a segment's sparse index by scanning `.nxrec`.
[[nodiscard]] std::vector<IndexEntry>
rebuild_index_entries(const SegmentInfo& segment, const std::string& directory,
                      std::uint64_t stride) {
    std::vector<IndexEntry> entries;
    std::ifstream input(directory + "/" + segment.rec_path, std::ios::binary);
    if (!input) {
        return entries;
    }
    std::uint64_t offset = 0;
    std::uint64_t start_offset = 0;
    std::uint64_t start_commit = 0;
    std::uint64_t min_event = 0;
    std::uint64_t max_event = 0;
    std::uint32_t count = 0;
    std::uint64_t mask = 0;
    while (true) {
        RecordHeader header{};
        const std::streamoff before = input.tellg();
        if (!read_header(input, header) || !valid_header(header)) {
            break;
        }
        std::vector<std::byte> payload{};
        const bool needs_payload =
            header.type == static_cast<std::uint16_t>(RecordType::VideoFrameRef);
        if (needs_payload) {
            if (header.payload_size > kMaxReaderPayload) {
                break;
            }
            payload.resize(header.payload_size);
            if (!payload.empty() && !read_exact(input, payload.data(), payload.size())) {
                break;
            }
        } else if (header.payload_size > 0U) {
            input.seekg(static_cast<std::streamoff>(header.payload_size), std::ios::cur);
            if (!input.good()) {
                break;
            }
        }
        const std::uint64_t event = event_of(header, payload);
        if (count == 0U) {
            start_offset = static_cast<std::uint64_t>(before);
            start_commit = header.commit_mono_ns;
            min_event = event;
            max_event = event;
            mask = channel_bit(header.channel_id);
        } else {
            min_event = std::min(min_event, event);
            max_event = std::max(max_event, event);
            mask |= channel_bit(header.channel_id);
        }
        ++count;
        offset = static_cast<std::uint64_t>(input.tellg());
        if (offset - start_offset >= stride) {
            IndexEntry entry{};
            entry.commit_ns = start_commit;
            entry.rec_offset = start_offset;
            entry.min_event_ns = min_event;
            entry.max_event_ns = max_event;
            entry.channel_mask = mask;
            entry.end_rec_offset = offset;
            entry.record_count = count;
            entry.entry_crc32c = detail::crc32c(&entry, offsetof(IndexEntry, entry_crc32c));
            entries.push_back(entry);
            count = 0;
            mask = 0;
        }
    }
    if (count > 0U) {
        IndexEntry entry{};
        entry.commit_ns = start_commit;
        entry.rec_offset = start_offset;
        entry.min_event_ns = min_event;
        entry.max_event_ns = max_event;
        entry.channel_mask = mask;
        entry.end_rec_offset = offset;
        entry.record_count = count;
        entry.entry_crc32c = detail::crc32c(&entry, offsetof(IndexEntry, entry_crc32c));
        entries.push_back(entry);
    }
    return entries;
}

[[nodiscard]] DatasetField parse_field(const nlohmann::json& entry) {
    DatasetField field{};
    if (!entry.is_object()) {
        return field;
    }
    core::detail::read_string(entry, "name", field.name);
    core::detail::read_string(entry, "type", field.type);
    core::detail::read_string(entry, "units", field.units);
    core::detail::read_unsigned(entry, "offset", field.offset);
    core::detail::read_unsigned(entry, "element_size", field.element_size);
    core::detail::read_unsigned(entry, "count", field.count);
    core::detail::read_unsigned(entry, "stride", field.stride);
    return field;
}

void parse_fields(const SchemaDeclare& schema, std::vector<DatasetField>& fields) {
    fields.clear();
    if (schema.encoding != "jsonschema" && schema.encoding != "nexigen.pod.v1") {
        return;
    }
    const std::string text(reinterpret_cast<const char*>(schema.data.data()), schema.data.size());
    const auto document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return;
    }
    const auto list = document.find("fields");
    if (list == document.end() || !list->is_array()) {
        return;
    }
    for (const auto& entry : *list) {
        if (entry.is_object()) {
            fields.push_back(parse_field(entry));
        }
    }
}

struct RecordCounts {
    std::uint64_t records{0};
    std::uint64_t gaps{0};
};

bool count_visitor(void* context, const RecordHeader& header,
                   std::span<const std::byte> /*payload*/, std::uint64_t /*offset*/) {
    auto& counts = *static_cast<RecordCounts*>(context);
    ++counts.records;
    if (header.type == static_cast<std::uint16_t>(RecordType::GapMarker)) {
        ++counts.gaps;
    }
    return true;
}

struct VideoCheck {
    const SessionReader* reader{nullptr};
    bool ok{true};
};

bool video_check_visitor(void* context, const RecordHeader& header,
                         std::span<const std::byte> payload, std::uint64_t /*offset*/) {
    if (header.type != static_cast<std::uint16_t>(RecordType::VideoFrameRef) ||
        payload.size() != sizeof(VideoFrameRef)) {
        return true;
    }
    auto& state = *static_cast<VideoCheck*>(context);
    VideoFrameRef reference{};
    std::memcpy(&reference, payload.data(), sizeof(reference));
    if (reference.vid_crc32c != 0U) {
        const auto bytes = state.reader->read_video(reference);
        state.ok = state.ok && bytes.has_value();
    }
    return state.ok;
}

/// @brief Require a closed segment to end in a valid `SegmentFooter`.
[[nodiscard]] core::Status verify_footer(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    const auto end = input.tellg();
    if (end < static_cast<std::streamoff>(sizeof(SegmentFooter))) {
        return std::unexpected(core::ErrorCode::CorruptData);
    }
    input.seekg(end - static_cast<std::streamoff>(sizeof(SegmentFooter)), std::ios::beg);
    SegmentFooter footer{};
    if (!read_exact(input, &footer, sizeof(footer)) || footer.magic != kFooterMagic ||
        footer.magic_end != kFooterMagic ||
        footer.footer_crc32c !=
            detail::crc32c(&footer, offsetof(SegmentFooter, footer_crc32c))) {
        return std::unexpected(core::ErrorCode::CorruptData);
    }
    return {};
}

/// @brief Locate the first invalid record in one segment.
[[nodiscard]] std::optional<VerifyFailure>
diagnose_records(const SegmentInfo& segment, const std::string& directory,
                 const SessionReader& reader) {
    std::ifstream input(directory + "/" + segment.rec_path, std::ios::binary);
    if (!input) {
        return VerifyFailure{segment.seq, 0, 0};
    }
    while (true) {
        const std::streamoff offset = input.tellg();
        RecordHeader header{};
        const auto where = VerifyFailure{segment.seq, static_cast<std::uint64_t>(offset), 0};
        if (!read_header(input, header) || !valid_header(header)) {
            return where;
        }
        if (header.payload_size > kMaxReaderPayload) {
            return VerifyFailure{segment.seq, where.offset, header.channel_id};
        }
        std::vector<std::byte> payload(header.payload_size);
        if (!payload.empty() && !read_exact(input, payload.data(), payload.size())) {
            return VerifyFailure{segment.seq, where.offset, header.channel_id};
        }
        if (header.payload_crc32c != detail::crc32c(payload.data(), payload.size())) {
            return VerifyFailure{segment.seq, where.offset, header.channel_id};
        }
        if (header.type == static_cast<std::uint16_t>(RecordType::VideoFrameRef) &&
            payload.size() == sizeof(VideoFrameRef)) {
            VideoFrameRef reference{};
            std::memcpy(&reference, payload.data(), sizeof(reference));
            if (reference.vid_crc32c != 0U && !reader.read_video(reference).has_value()) {
                return VerifyFailure{segment.seq, where.offset, header.channel_id};
            }
        }
    }
}

/// @brief Validate a closed segment's trailing footer.
[[nodiscard]] std::optional<VerifyFailure>
diagnose_footer(const SegmentInfo& segment, const std::string& directory) {
    std::ifstream tail(directory + "/" + segment.rec_path, std::ios::binary | std::ios::ate);
    if (!tail) {
        return VerifyFailure{segment.seq, 0, 0};
    }
    const auto end = tail.tellg();
    if (end < static_cast<std::streamoff>(sizeof(SegmentFooter))) {
        return VerifyFailure{segment.seq, 0, 0};
    }
    tail.seekg(end - static_cast<std::streamoff>(sizeof(SegmentFooter)), std::ios::beg);
    SegmentFooter footer{};
    if (!read_exact(tail, &footer, sizeof(footer)) || footer.magic != kFooterMagic ||
        footer.magic_end != kFooterMagic ||
        footer.footer_crc32c != detail::crc32c(&footer, offsetof(SegmentFooter, footer_crc32c))) {
        return VerifyFailure{segment.seq, static_cast<std::uint64_t>(end), 0};
    }
    return std::nullopt;
}

/// @brief Locate the first record/footer that fails validation.
[[nodiscard]] std::optional<VerifyFailure>
diagnose_failure(const SessionManifest& manifest, const std::string& directory,
                 const SessionReader& reader) {
    for (const auto& segment : manifest.segments) {
        if (const auto record = diagnose_records(segment, directory, reader);
            record.has_value()) {
            return record;
        }
        if (segment.closed) {
            if (const auto footer = diagnose_footer(segment, directory); footer.has_value()) {
                return footer;
            }
        }
    }
    return std::nullopt;
}

/// @brief Read the leading schema/channel declarations of one bare segment.
[[nodiscard]] core::Status read_segment_declarations(const std::string& path,
                                                     SessionManifest& manifest) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    while (true) {
        RecordHeader header{};
        if (!read_header(input, header) || !valid_header(header)) {
            return {};
        }
        const auto type = static_cast<RecordType>(header.type);
        const bool is_schema = type == RecordType::SchemaDeclare;
        const bool is_channel = type == RecordType::ChannelDeclare;
        if (!is_schema && !is_channel) {
            return {};
        }
        if (header.payload_size > kMaxReaderPayload) {
            return std::unexpected(core::ErrorCode::CorruptData);
        }
        std::vector<std::byte> payload(header.payload_size);
        if (!payload.empty() && !read_exact(input, payload.data(), payload.size())) {
            return std::unexpected(core::ErrorCode::CorruptData);
        }
        if (header.payload_crc32c != detail::crc32c(payload.data(), payload.size())) {
            return std::unexpected(core::ErrorCode::SecurityError);
        }
        const std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
        const auto document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded() || !document.is_object()) {
            return std::unexpected(core::ErrorCode::CorruptData);
        }
        if (is_schema) {
            manifest.schemas.push_back(document.get<SchemaDeclare>());
        } else {
            manifest.channels.push_back(document.get<ChannelDeclare>());
        }
    }
}

} // namespace

SessionReader::SessionReader(std::string directory, SessionManifest manifest) noexcept
    : directory_(std::move(directory)), manifest_(std::move(manifest)) {}

core::Result<SessionReader> SessionReader::open(const std::string& session_directory) noexcept {
    std::ifstream input(session_directory + "/session.json");
    if (!input) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    const auto document = nlohmann::json::parse(input, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    SessionManifest manifest{};
    from_json(document, manifest);
    if (!manifest.validate().has_value()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return SessionReader(session_directory, std::move(manifest));
}

core::Result<SessionReader> SessionReader::open_segment(const std::string& rec_path) noexcept {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(rec_path, error);
    const std::string resolved = error ? rec_path : absolute.string();
    const auto slash = resolved.find_last_of('/');
    const std::string directory = slash == std::string::npos ? std::string(".")
                                                               : resolved.substr(0, slash);
    const std::string file = slash == std::string::npos ? resolved : resolved.substr(slash + 1U);
    if (file.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const auto dot = file.find_last_of('.');
    const std::string stem = dot == std::string::npos ? file : file.substr(0, dot);

    // `VideoFrameRef.vid_segment_seq` names the manifest sequence, so a
    // standalone segment must keep the `_<seq>` suffix from its file name.
    std::uint32_t seq = 0;
    const auto underscore = stem.find_last_of('_');
    if (underscore != std::string::npos && underscore + 1U < stem.size()) {
        const char* first = stem.data() + underscore + 1U;
        const char* last = stem.data() + stem.size();
        std::uint32_t parsed = 0;
        const auto result = std::from_chars(first, last, parsed);
        if (result.ec == std::errc{} && result.ptr == last) {
            seq = parsed;
        }
    }

    SessionManifest manifest{};
    manifest.session_id = "segment";
    manifest.settings.session_id = "segment";
    manifest.settings.session_root = directory;
    SegmentInfo segment{};
    segment.seq = seq;
    segment.rec_path = file;
    segment.idx_path = stem + ".nxidx";
    segment.vid_path = stem + ".nxvid";
    manifest.segments.push_back(segment);

    const auto declarations = read_segment_declarations(resolved, manifest);
    if (!declarations.has_value()) {
        return std::unexpected(declarations.error());
    }
    if (manifest.channels.empty() || !manifest.validate().has_value()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return SessionReader(directory, std::move(manifest));
}

std::string SessionReader::rec_path(std::uint32_t segment_seq) const {
    for (const auto& segment : manifest_.segments) {
        if (segment.seq == segment_seq) {
            return directory_ + "/" + segment.rec_path;
        }
    }
    return {};
}

std::string SessionReader::vid_path(std::uint32_t segment_seq) const {
    for (const auto& segment : manifest_.segments) {
        if (segment.seq == segment_seq) {
            return directory_ + "/" + segment.vid_path;
        }
    }
    return {};
}

namespace {

[[nodiscard]] DatasetInfo make_dataset(const ChannelDeclare& channel,
                                       const std::vector<SchemaDeclare>& schemas) {
    DatasetInfo info{};
    info.channel_id = channel.id;
    info.kind = channel.kind;
    info.name = channel.topic;
    info.schema_id = channel.schema_id;
    info.message_encoding = channel.message_encoding;
    for (const auto& schema : schemas) {
        if (schema.id == channel.schema_id) {
            info.schema_name = schema.name;
            info.schema_encoding = schema.encoding;
            parse_fields(schema, info.fields);
            break;
        }
    }
    return info;
}

void add_epoch(DatasetInfo& dataset, std::uint64_t epoch);

void apply_record(DatasetInfo& dataset, const RecordHeader& header,
                  std::span<const std::byte> payload, std::uint64_t event,
                  std::uint64_t record_offset, const SegmentInfo& segment) {
    ++dataset.records;
    const bool is_gap = header.type == static_cast<std::uint16_t>(RecordType::GapMarker);
    if (is_gap) {
        ++dataset.gaps;
    }
    std::uint64_t payload_bytes = header.payload_size;
    if (header.type == static_cast<std::uint16_t>(RecordType::VideoFrameRef) &&
        payload.size() == sizeof(VideoFrameRef)) {
        VideoFrameRef reference{};
        std::memcpy(&reference, payload.data(), sizeof(reference));
        if (reference.codec != static_cast<std::uint16_t>(VideoCodec::Unknown)) {
            dataset.codec = static_cast<VideoCodec>(reference.codec);
        }
        if (reference.bitstream != static_cast<std::uint16_t>(BitstreamFormat::Unknown)) {
            dataset.bitstream = static_cast<BitstreamFormat>(reference.bitstream);
        }
        if (reference.width != 0U) {
            dataset.width = reference.width;
        }
        if (reference.height != 0U) {
            dataset.height = reference.height;
        }
        add_epoch(dataset, reference.config_epoch);
        payload_bytes = reference.vid_size;
    }
    dataset.payload_bytes += payload_bytes;
    if (dataset.records == 1U) {
        dataset.first_event_ns = event;
        dataset.first_rec_offset = record_offset;
    }
    dataset.last_event_ns = event;
    dataset.last_rec_offset = record_offset;

    auto part = std::find_if(dataset.segments.begin(), dataset.segments.end(),
                             [&](const DatasetSegmentInfo& value) {
                                 return value.seq == segment.seq;
                             });
    if (part == dataset.segments.end()) {
        DatasetSegmentInfo created{};
        created.seq = segment.seq;
        created.path = segment.rec_path;
        created.records = 1;
        created.gaps = is_gap ? 1U : 0U;
        created.bytes = payload_bytes;
        created.start_event_ns = event;
        created.end_event_ns = event;
        created.closed = segment.closed;
        dataset.segments.push_back(created);
        dataset.closed = dataset.closed && segment.closed;
        return;
    }
    ++part->records;
    if (is_gap) {
        ++part->gaps;
    }
    part->bytes += payload_bytes;
    if (part->records == 1U) {
        part->start_event_ns = event;
    }
    part->end_event_ns = event;
    part->closed = segment.closed;
    dataset.closed = dataset.closed && segment.closed;
}

/// @brief Keep the observed `config_epoch` set ordered and duplicate-free.
void add_epoch(DatasetInfo& dataset, std::uint64_t epoch) {
    if (std::find(dataset.config_epochs.begin(), dataset.config_epochs.end(), epoch) ==
        dataset.config_epochs.end()) {
        dataset.config_epochs.push_back(epoch);
    }
}

void process_video_config(std::span<const std::byte> payload,
                          std::vector<DatasetInfo>& datasets, std::uint32_t channel_id) {
    if (payload.size() != sizeof(VideoStreamConfig)) {
        return;
    }
    VideoStreamConfig config{};
    std::memcpy(&config, payload.data(), sizeof(config));
    const auto dataset = std::find_if(datasets.begin(), datasets.end(),
                                      [&](const DatasetInfo& value) {
                                          return value.channel_id == channel_id;
                                      });
    if (dataset == datasets.end()) {
        return;
    }
    dataset->extradata_size = config.extradata_size;
    dataset->codec = static_cast<VideoCodec>(config.codec);
    dataset->bitstream = static_cast<BitstreamFormat>(config.bitstream);
    dataset->width = config.width;
    dataset->height = config.height;
    add_epoch(*dataset, config.config_epoch);
}

void process_inventory_record(const RecordHeader& header, std::span<const std::byte> payload,
                              std::uint64_t record_offset, const SegmentInfo& segment,
                              std::vector<SchemaDeclare>& schemas,
                              std::vector<DatasetInfo>& datasets) {
    if (header.type == static_cast<std::uint16_t>(RecordType::SchemaDeclare)) {
        const std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
        const auto document = nlohmann::json::parse(text, nullptr, false);
        if (!document.is_discarded() && document.is_object()) {
            SchemaDeclare schema{};
            from_json(document, schema);
            schemas.push_back(std::move(schema));
        }
        return;
    }
    if (header.type == static_cast<std::uint16_t>(RecordType::ChannelDeclare)) {
        const std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
        const auto document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded() || !document.is_object()) {
            return;
        }
        ChannelDeclare channel{};
        from_json(document, channel);
        const auto existing = std::find_if(datasets.begin(), datasets.end(),
                                           [&](const DatasetInfo& value) {
                                               return value.channel_id == channel.id;
                                           });
        if (existing == datasets.end()) {
            datasets.push_back(make_dataset(channel, schemas));
        }
        return;
    }
    if (header.type == static_cast<std::uint16_t>(RecordType::VideoStreamConfig)) {
        process_video_config(payload, datasets, header.channel_id);
        return;
    }
    const auto dataset = std::find_if(datasets.begin(), datasets.end(),
                                      [&](const DatasetInfo& value) {
                                          return value.channel_id == header.channel_id;
                                      });
    if (dataset != datasets.end()) {
        apply_record(*dataset, header, payload, event_of(header, payload), record_offset, segment);
    }
}

void scan_inventory_segment(const SegmentInfo& segment, const std::string& directory,
                            std::vector<SchemaDeclare>& schemas,
                            std::vector<DatasetInfo>& datasets) {
    std::ifstream input(directory + "/" + segment.rec_path, std::ios::binary);
    if (!input) {
        return;
    }
    while (true) {
        RecordHeader header{};
        const std::streamoff before = input.tellg();
        if (!read_header(input, header) || !valid_header(header)) {
            return;
        }
        std::vector<std::byte> payload{};
        const bool needs_payload =
            header.type == static_cast<std::uint16_t>(RecordType::SchemaDeclare) ||
            header.type == static_cast<std::uint16_t>(RecordType::ChannelDeclare) ||
            header.type == static_cast<std::uint16_t>(RecordType::VideoStreamConfig) ||
            header.type == static_cast<std::uint16_t>(RecordType::VideoFrameRef);
        if (needs_payload) {
            if (header.payload_size > kMaxReaderPayload) {
                return;
            }
            payload.resize(header.payload_size);
            if (!payload.empty() && !read_exact(input, payload.data(), payload.size())) {
                return;
            }
            if (header.payload_crc32c != detail::crc32c(payload.data(), payload.size())) {
                return;
            }
        } else if (header.payload_size > 0U) {
            input.seekg(static_cast<std::streamoff>(header.payload_size), std::ios::cur);
            if (!input.good()) {
                return;
            }
        }
        process_inventory_record(header, payload, static_cast<std::uint64_t>(before), segment,
                                 schemas, datasets);
    }
}

} // namespace

core::Result<std::vector<DatasetInfo>> SessionReader::datasets() const noexcept {
    std::vector<SchemaDeclare> schemas = manifest_.schemas;
    std::vector<DatasetInfo> datasets;
    for (const auto& channel : manifest_.channels) {
        datasets.push_back(make_dataset(channel, schemas));
    }
    for (const auto& segment : manifest_.segments) {
        scan_inventory_segment(segment, directory_, schemas, datasets);
    }
    std::sort(datasets.begin(), datasets.end(),
              [](const DatasetInfo& lhs, const DatasetInfo& rhs) {
                  return lhs.channel_id < rhs.channel_id;
              });
    return datasets;
}

namespace {

[[nodiscard]] bool channel_declared(const SessionManifest& manifest, std::uint32_t id) {
    for (const auto& channel : manifest.channels) {
        if (channel.id == id) {
            return true;
        }
    }
    return false;
}

struct RecordFilter {
    std::uint32_t channel_id{0};
    bool has_channel{false};
    std::uint64_t start_event_ns{0};
    std::uint64_t end_event_ns{0};
    bool has_range{false};
};

/// @brief Read one record and dispatch it; `false` means EOF/torn (stop cleanly).
[[nodiscard]] core::Result<bool> visit_record(std::ifstream& input, const SessionManifest& manifest,
                                             const RecordFilter& filter, RecordVisitor visitor,
                                             void* context) {
    RecordHeader header{};
    const std::streamoff before = input.tellg();
    if (!read_header(input, header) || !valid_header(header)) {
        return false;
    }
    if (header.payload_size > kMaxReaderPayload) {
        return std::unexpected(core::ErrorCode::CorruptData);
    }
    std::vector<std::byte> payload(header.payload_size);
    if (!payload.empty() && !read_exact(input, payload.data(), payload.size())) {
        return false;
    }
    if (header.payload_crc32c != detail::crc32c(payload.data(), payload.size())) {
        return std::unexpected(core::ErrorCode::SecurityError);
    }
    if (header.channel_id != 0U && !channel_declared(manifest, header.channel_id)) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    const std::uint64_t event = event_of(header, payload);
    const bool channel_match = !filter.has_channel || header.channel_id == filter.channel_id;
    const bool range_match = !filter.has_range ||
                             (event >= filter.start_event_ns && event <= filter.end_event_ns);
    if (!channel_match || !range_match) {
        return true;
    }
    return visitor(context, header, std::span<const std::byte>(payload),
                   static_cast<std::uint64_t>(before));
}

/// @brief First record offset whose index block may cover `start_event_ns`.
///
/// A missing or empty `.nxidx` is rebuilt from `.nxrec` before searching.
/// Returns `NotFound` when no block can cover the requested range.
[[nodiscard]] core::Result<std::streamoff>
range_start_offset(const SegmentInfo& segment, const std::string& directory,
                   std::uint64_t start_event_ns, std::uint64_t end_event_ns,
                   std::uint64_t index_stride_bytes) {
    auto entries = read_index(directory + "/" + segment.idx_path);
    if (entries.empty()) {
        entries = rebuild_index_entries(segment, directory, index_stride_bytes);
    }
    // Index intervals are ordered by file position, not by event time. With
    // out-of-order events a binary search on `max_event_ns` is invalid, so
    // collect every intersecting interval and seek to the earliest one.
    std::optional<std::streamoff> earliest;
    for (const auto& entry : entries) {
        if (entry.max_event_ns < start_event_ns || entry.min_event_ns > end_event_ns) {
            continue;
        }
        const auto offset = static_cast<std::streamoff>(entry.rec_offset);
        if (!earliest.has_value() || offset < *earliest) {
            earliest = offset;
        }
    }
    if (!earliest.has_value()) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    return *earliest;
}

core::Status visit_segment(const SegmentInfo& segment, const std::string& directory,
                           const SessionManifest& manifest, const RecordFilter& filter,
                           RecordVisitor visitor, void* context) {
    std::ifstream input(directory + "/" + segment.rec_path, std::ios::binary);
    if (!input) {
        return {};
    }
    if (filter.has_range) {
        const auto offset = range_start_offset(segment, directory, filter.start_event_ns,
                                               filter.end_event_ns,
                                               manifest.settings.index_stride_bytes);
        if (!offset.has_value()) {
            return offset.error() == core::ErrorCode::NotFound
                       ? core::Status{}
                       : std::unexpected(offset.error());
        }
        input.seekg(*offset, std::ios::beg);
        if (!input.good()) {
            return {};
        }
    }
    while (true) {
        const auto step = visit_record(input, manifest, filter, visitor, context);
        if (!step.has_value()) {
            return std::unexpected(step.error());
        }
        if (!*step) {
            return {};
        }
    }
}

} // namespace

core::Status SessionReader::for_each(std::uint32_t channel_id, bool has_channel,
                                     std::uint64_t start_event_ns, std::uint64_t end_event_ns,
                                     bool has_range, RecordVisitor visitor,
                                     void* context) const noexcept {
    if (visitor == nullptr) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const RecordFilter filter{channel_id, has_channel, start_event_ns, end_event_ns, has_range};
    for (const auto& segment : manifest_.segments) {
        const auto status = visit_segment(segment, directory_, manifest_, filter, visitor,
                                          context);
        if (!status.has_value()) {
            return status;
        }
    }
    return {};
}

namespace {

struct EpochState {
    std::uint32_t channel_id{0};
    std::uint64_t publisher_id{0};
    std::uint64_t first_sequence{0};
    std::uint64_t last_sequence{0};
    std::uint64_t records{0};
};

bool epoch_visitor(void* context, const RecordHeader& header,
                   std::span<const std::byte> /*payload*/, std::uint64_t /*offset*/) {
    if (header.channel_id == 0U) {
        return true;
    }
    auto& epochs = *static_cast<std::vector<EpochState>*>(context);
    auto epoch = std::find_if(epochs.rbegin(), epochs.rend(),
                              [&](const EpochState& value) {
                                  return value.channel_id == header.channel_id;
                              });
    if (epoch == epochs.rend() || epoch->publisher_id != header.publisher_id ||
        header.sequence <= epoch->last_sequence) {
        epochs.push_back(EpochState{header.channel_id, header.publisher_id, header.sequence,
                                    header.sequence, 1});
    } else {
        epoch->last_sequence = header.sequence;
        ++epoch->records;
    }
    return true;
}

} // namespace

core::Result<std::vector<PublisherEpoch>> SessionReader::publisher_epochs() const noexcept {
    std::vector<EpochState> states;
    const auto status = for_each(0, false, 0, 0, false, epoch_visitor, &states);
    if (!status.has_value()) {
        return std::unexpected(status.error());
    }
    std::vector<PublisherEpoch> epochs;
    epochs.reserve(states.size());
    for (const auto& state : states) {
        PublisherEpoch epoch{};
        epoch.channel_id = state.channel_id;
        epoch.publisher_id = state.publisher_id;
        epoch.first_sequence = state.first_sequence;
        epoch.last_sequence = state.last_sequence;
        epoch.records = state.records;
        epochs.push_back(epoch);
    }
    return epochs;
}

core::Result<std::vector<std::byte>>
SessionReader::read_video(const VideoFrameRef& reference) const noexcept {
    const std::string path = vid_path(reference.vid_segment_seq);
    if (path.empty()) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    if (reference.vid_size > kMaxReaderPayload) {
        return std::unexpected(core::ErrorCode::CorruptData);
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    input.seekg(static_cast<std::streamoff>(reference.vid_offset), std::ios::beg);
    std::vector<std::byte> bytes(reference.vid_size);
    if (!bytes.empty() && !read_exact(input, bytes.data(), bytes.size())) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (reference.vid_crc32c != 0U &&
        reference.vid_crc32c != detail::crc32c(bytes.data(), bytes.size())) {
        return std::unexpected(core::ErrorCode::SecurityError);
    }
    return bytes;
}

core::Result<std::vector<IndexEntry>>
SessionReader::recover_index(std::uint32_t segment_seq) const noexcept {
    for (const auto& segment : manifest_.segments) {
        if (segment.seq == segment_seq) {
            return rebuild_index_entries(segment, directory_,
                                         manifest_.settings.index_stride_bytes);
        }
    }
    return std::unexpected(core::ErrorCode::NotFound);
}

core::Status SessionReader::verify(std::uint64_t& records, std::uint64_t& gaps,
                                   VerifyFailure* failure) const noexcept {
    const auto fail = [&](core::ErrorCode code) -> core::Status {
        if (failure != nullptr) {
            const auto spot = diagnose_failure(manifest_, directory_, *this);
            *failure = spot.value_or(VerifyFailure{});
        }
        return std::unexpected(code);
    };
    RecordCounts counts{};
    const auto status = for_each(0, false, 0, 0, false, count_visitor, &counts);
    if (!status.has_value()) {
        return fail(status.error());
    }
    VideoCheck video{this, true};
    const auto video_status = for_each(0, false, 0, 0, false, video_check_visitor, &video);
    if (!video_status.has_value() || !video.ok) {
        return fail(core::ErrorCode::SecurityError);
    }
    std::uint64_t manifest_records = 0;
    for (const auto& segment : manifest_.segments) {
        if (!segment.closed) {
            continue;
        }
        const auto footer = verify_footer(directory_ + "/" + segment.rec_path);
        if (!footer.has_value()) {
            return fail(footer.error());
        }
        manifest_records += segment.records;
    }
    records = counts.records;
    gaps = counts.gaps;
    if (manifest_records != 0U && records != manifest_records) {
        if (failure != nullptr) {
            failure->segment_seq =
                manifest_.segments.empty() ? 0U : manifest_.segments.back().seq;
        }
        return std::unexpected(core::ErrorCode::CorruptData);
    }
    return {};
}

} // namespace nexigen::recorder
