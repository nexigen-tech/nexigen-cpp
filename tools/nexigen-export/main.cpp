// nexigen-export — on-demand converter for raw recorder sessions.
//
// One invocation reads the session, writes the requested output, and exits.
// There is no daemon, no follow mode, and no background thread.
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/logging/log_frame.hpp>
#include <nexigen/metrics/metric_frame.hpp>
#include <nexigen/recorder/dataset_info.hpp>
#include <nexigen/recorder/export_selection.hpp>
#include <nexigen/recorder/payload_decoder.hpp>
#include <nexigen/recorder/recorder_types.hpp>
#include <nexigen/recorder/schema_declare.hpp>
#include <nexigen/recorder/session_reader.hpp>
#include <nexigen/recorder/wire_types.hpp>
#include <nexigen/tracing/span_frame.hpp>

#if defined(NEXIGEN_HAS_MCAP)
#include "mcap_output.hpp"
#endif

namespace {

using nexigen::recorder::DatasetInfo;
using nexigen::recorder::RecordHeader;
using nexigen::recorder::RecordType;
using nexigen::recorder::SessionReader;
using nexigen::recorder::VideoFrameRef;

struct Options {
    std::string command{};
    std::string session{};
    nexigen::recorder::ExportSelection selection{};
    std::string output{};
    std::string format{"jsonl"};
    bool json{false};
    bool help{false};
};

[[nodiscard]] bool parse_u64(std::string_view text, std::uint64_t& out) {
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] bool is_value_option(std::string_view arg) {
    return arg == "--dataset" || arg == "--channels" || arg == "--output" || arg == "-o" ||
           arg == "--format" || arg == "--start" || arg == "--end";
}

[[nodiscard]] bool is_live_option(std::string_view arg) {
    return arg == "--follow" || arg == "--watch" || arg == "--live" ||
           arg.starts_with("--follow=") || arg.starts_with("--watch=") ||
           arg.starts_with("--live=");
}

void split_channels(std::string_view value, std::vector<std::string>& out) {
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto comma = value.find(',', start);
        const auto part = value.substr(
            start, comma == std::string_view::npos ? value.size() - start : comma - start);
        if (!part.empty()) {
            out.emplace_back(part);
        }
        if (comma == std::string_view::npos) {
            return;
        }
        start = comma + 1U;
    }
}

[[nodiscard]] bool parse_bound(std::string_view value, std::uint64_t& out, bool& has) {
    if (value.empty() || !parse_u64(value, out)) {
        return false;
    }
    has = true;
    return true;
}

[[nodiscard]] bool apply_value_option(std::string_view arg, std::string_view value,
                                      Options& options) {
    if (arg == "--dataset") {
        options.selection.dataset = std::string(value);
    } else if (arg == "--channels") {
        split_channels(value, options.selection.channels);
    } else if (arg == "--output" || arg == "-o") {
        options.output = std::string(value);
    } else if (arg == "--format") {
        options.format = std::string(value);
    } else if (arg == "--start") {
        return parse_bound(value, options.selection.start_event_ns,
                           options.selection.has_start);
    } else if (arg == "--end") {
        return parse_bound(value, options.selection.end_event_ns, options.selection.has_end);
    } else {
        return false;
    }
    return true;
}

[[nodiscard]] bool valid_command(std::string_view command) {
    return command == "inspect" || command == "verify" || command == "list" ||
           command == "info" || command == "extract" || command == "jsonl" ||
           command == "perfetto" || command == "video";
}

[[nodiscard]] bool parse_options(int argc, char** argv, Options& options, std::string& error) {
    const auto fail = [&error](std::string text) {
        error = std::move(text);
        return false;
    };
    if (argc < 2) {
        return fail("missing command");
    }
    options.command = argv[1];
    if (options.command == "--help" || options.command == "-h") {
        options.help = true;
        return true;
    }
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (is_live_option(arg)) {
            return fail(std::string(arg) +
                        ": live conversion is not supported; nexigen-export runs on demand");
        }
        if (is_value_option(arg)) {
            if (i + 1 >= argc) {
                return fail(std::string(arg) + ": missing value");
            }
            const std::string_view value(argv[++i]);
            if (!apply_value_option(arg, value, options)) {
                return fail(std::string(arg) + ": invalid value: " + std::string(value));
            }
            continue;
        }
        if (arg == "--json") {
            options.json = true;
        } else if (arg == "--help" || arg == "-h") {
            options.help = true;
            return true;
        } else if (!arg.empty() && arg[0] != '-') {
            options.session = std::string(arg);
        } else {
            return fail("unknown argument: " + std::string(arg));
        }
    }
    if (!valid_command(options.command)) {
        return fail("unknown command: " + options.command);
    }
    return true;
}

void print_usage(std::FILE* stream) {
    std::fprintf(stream,
                 "usage: nexigen-export <command> <session-dir> [options]\n"
                 "commands: list, info, extract, jsonl, perfetto, video, inspect, verify\n"
                 "options:\n"
                 "  --dataset <id|name>  select a dataset\n"
                 "  --channels <list>    additional dataset ids/names/topics (comma separated)\n"
                 "  --format <name>      jsonl | perfetto | elementary | mcap\n"
                 "                       (mp4/mkv are deferred until nexigen_media)\n"
                 "  --output, -o <path>  output file (required by extract/jsonl/perfetto/video)\n"
                 "  --start <ns>         inclusive start event time\n"
                 "  --end <ns>           inclusive end event time\n"
                 "  --json               JSON output for list/info/inspect\n"
                 "  --help               print this help\n");
}

[[nodiscard]] const DatasetInfo* find_dataset(const std::vector<DatasetInfo>& datasets,
                                              std::string_view selection) {
    if (selection.empty()) {
        return nullptr;
    }
    for (const auto& dataset : datasets) {
        if (std::to_string(dataset.channel_id) == selection || dataset.name == selection) {
            return &dataset;
        }
    }
    return nullptr;
}

[[nodiscard]] const nexigen::recorder::SchemaDeclare*
find_schema(const SessionReader& reader, std::uint32_t schema_id) {
    for (const auto& schema : reader.manifest().schemas) {
        if (schema.id == schema_id) {
            return &schema;
        }
    }
    return nullptr;
}

/// @brief Print the available dataset ids/names for a failed selection.
void print_available(const std::vector<DatasetInfo>& datasets) {
    std::fprintf(stderr, "nexigen-export: available datasets:");
    for (const auto& dataset : datasets) {
        std::fprintf(stderr, " %u(%s)", dataset.channel_id, dataset.name.c_str());
    }
    std::fprintf(stderr, "\n");
}

[[nodiscard]] nexigen::core::Status visit_dataset(const SessionReader& reader,
                                                 const Options& options,
                                                 std::uint32_t channel_id,
                                                 nexigen::recorder::RecordVisitor visitor,
                                                 void* context);

[[nodiscard]] std::string hex(std::span<const std::byte> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2U);
    for (const std::byte byte : bytes) {
        const auto value = static_cast<std::uint8_t>(byte);
        out.push_back(digits[value >> 4U]);
        out.push_back(digits[value & 0x0FU]);
    }
    return out;
}

struct JsonlContext {
    std::ostream* output{nullptr};
    const std::vector<nexigen::recorder::DatasetField>* fields{nullptr};
    std::span<const std::uint8_t> descriptor{};
    std::string_view encoding{};
    bool failed{false};
};

[[nodiscard]] bool typed_payload(const RecordHeader& header, std::span<const std::byte> payload,
                                 nlohmann::json& out) {
    if (header.type == static_cast<std::uint16_t>(RecordType::LogFrame) &&
        payload.size() == sizeof(nexigen::logging::LogFrame)) {
        nexigen::logging::LogFrame frame{};
        std::memcpy(&frame, payload.data(), sizeof(frame));
        out = frame;
        return true;
    }
    if (header.type == static_cast<std::uint16_t>(RecordType::MetricFrame) &&
        payload.size() == sizeof(nexigen::metrics::MetricFrame)) {
        nexigen::metrics::MetricFrame frame{};
        std::memcpy(&frame, payload.data(), sizeof(frame));
        out = frame;
        return true;
    }
    if (header.type == static_cast<std::uint16_t>(RecordType::SpanFrame) &&
        payload.size() == sizeof(nexigen::tracing::SpanFrame)) {
        nexigen::tracing::SpanFrame frame{};
        std::memcpy(&frame, payload.data(), sizeof(frame));
        out = frame;
        return true;
    }
    if (header.type == static_cast<std::uint16_t>(RecordType::VideoFrameRef) &&
        payload.size() == sizeof(nexigen::recorder::VideoFrameRef)) {
        nexigen::recorder::VideoFrameRef frame{};
        std::memcpy(&frame, payload.data(), sizeof(frame));
        out = frame;
        return true;
    }
    if (header.type == static_cast<std::uint16_t>(RecordType::VideoStreamConfig) &&
        payload.size() == sizeof(nexigen::recorder::VideoStreamConfig)) {
        nexigen::recorder::VideoStreamConfig frame{};
        std::memcpy(&frame, payload.data(), sizeof(frame));
        out = frame;
        return true;
    }
    if (header.type == static_cast<std::uint16_t>(RecordType::GapMarker) &&
        payload.size() == sizeof(nexigen::recorder::GapMarkerPayload)) {
        nexigen::recorder::GapMarkerPayload frame{};
        std::memcpy(&frame, payload.data(), sizeof(frame));
        out = frame;
        return true;
    }
    if (header.type == static_cast<std::uint16_t>(RecordType::TimeSync) &&
        payload.size() == sizeof(nexigen::recorder::TimeSyncPayload)) {
        nexigen::recorder::TimeSyncPayload frame{};
        std::memcpy(&frame, payload.data(), sizeof(frame));
        out = frame;
        return true;
    }
    return false;
}

nlohmann::json make_line(const RecordHeader& header, std::span<const std::byte> payload,
                         std::uint64_t offset, const JsonlContext* state = nullptr) {
    nlohmann::json line{};
    line["type"] = static_cast<std::uint16_t>(header.type);
    line["channel"] = header.channel_id;
    line["sequence"] = header.sequence;
    line["publisher_id"] = header.publisher_id;
    line["commit_mono_ns"] = header.commit_mono_ns;
    line["offset"] = offset;
    nlohmann::json decoded{};
    if (typed_payload(header, payload, decoded)) {
        line["payload"] = std::move(decoded);
        return line;
    }
    if (state != nullptr && state->fields != nullptr && !state->encoding.empty()) {
        const auto view = nexigen::recorder::decode_payload(
            state->encoding, state->descriptor, *state->fields, payload);
        if (view.has_value() &&
            view->kind == nexigen::recorder::PayloadKind::Json) {
            auto parsed = nlohmann::json::parse(view->text, nullptr, false);
            if (!parsed.is_discarded()) {
                line["payload"] = std::move(parsed);
                line["encoding"] = state->encoding;
                return line;
            }
        } else if (view.has_value()) {
            line["payload_base64"] = view->text;
            line["encoding"] = state->encoding;
            return line;
        } else {
            line["decode_error"] = nexigen::core::to_string(view.error());
            line["encoding"] = state->encoding;
        }
    }
    line["payload_hex"] = hex(payload);
    return line;
}

bool jsonl_visitor(void* context, const RecordHeader& header, std::span<const std::byte> payload,
                   std::uint64_t offset) {
    auto& state = *static_cast<JsonlContext*>(context);
    const auto line = make_line(header, payload, offset, &state);
    (*state.output) << line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
    if (!*state.output) {
        state.failed = true;
        return false;
    }
    return true;
}

#if defined(NEXIGEN_HAS_MCAP)
struct McapContext {
    nexigen::exportc::McapOutput* output{nullptr};
    std::uint16_t channel_id{0};
    bool opaque{false};
    std::int64_t utc_offset_ns{0};
    bool has_utc{false};
    bool failed{false};
};

[[nodiscard]] std::uint64_t mcap_time(const McapContext& state, std::uint64_t mono_ns) noexcept {
    if (!state.has_utc) {
        return mono_ns;
    }
    if (state.utc_offset_ns >= 0) {
        return mono_ns + static_cast<std::uint64_t>(state.utc_offset_ns);
    }
    const auto magnitude = static_cast<std::uint64_t>(-state.utc_offset_ns);
    return mono_ns > magnitude ? mono_ns - magnitude : 0U;
}

bool mcap_visitor(void* context, const RecordHeader& header, std::span<const std::byte> payload,
                  std::uint64_t offset) {
    auto& state = *static_cast<McapContext*>(context);
    const std::uint64_t time = mcap_time(state, header.commit_mono_ns);
    const auto sequence = static_cast<std::uint32_t>(header.sequence);
    if (state.opaque) {
        if (!state.output->write_bytes(state.channel_id, sequence, time, time, payload)) {
            state.failed = true;
            return false;
        }
        return true;
    }
    const auto line = make_line(header, payload, offset);
    const std::string text = line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (!state.output->write(state.channel_id, sequence, time, time, text)) {
        state.failed = true;
        return false;
    }
    return true;
}

struct TimeSyncState {
    bool found{false};
    std::int64_t offset_ns{0};
};

bool time_sync_visitor(void* context, const RecordHeader& header,
                       std::span<const std::byte> payload, std::uint64_t /*offset*/) {
    if (header.type != static_cast<std::uint16_t>(RecordType::TimeSync) ||
        payload.size() != sizeof(nexigen::recorder::TimeSyncPayload)) {
        return true;
    }
    auto& state = *static_cast<TimeSyncState*>(context);
    nexigen::recorder::TimeSyncPayload sync{};
    std::memcpy(&sync, payload.data(), sizeof(sync));
    state.offset_ns = sync.utc_offset_ns;
    state.found = true;
    return true;
}

[[nodiscard]] TimeSyncState read_time_sync(const SessionReader& reader) {
    TimeSyncState state{};
    static_cast<void>(reader.for_each(0, false, 0, 0, false, time_sync_visitor, &state));
    return state;
}
#endif

struct PerfettoContext {
    std::vector<nlohmann::json>* events{nullptr};
    bool failed{false};
};

bool perfetto_visitor(void* context, const RecordHeader& header,
                      std::span<const std::byte> payload, std::uint64_t /*offset*/) {
    if (header.type != static_cast<std::uint16_t>(RecordType::SpanFrame) ||
        payload.size() != sizeof(nexigen::tracing::SpanFrame)) {
        return true;
    }
    auto& state = *static_cast<PerfettoContext*>(context);
    nexigen::tracing::SpanFrame frame{};
    std::memcpy(&frame, payload.data(), sizeof(frame));
    const std::uint64_t duration = frame.end_ns > frame.start_ns ? frame.end_ns - frame.start_ns : 0;
    nlohmann::json event{};
    event["ph"] = "X";
    event["name"] = std::string(frame.name);
    event["ts"] = frame.start_ns / 1000U;
    event["dur"] = duration / 1000U;
    event["pid"] = frame.pid;
    event["tid"] = frame.thread_id;
    state.events->push_back(std::move(event));
    return true;
}

int run_perfetto(const SessionReader& reader, const Options& options,
                 const std::vector<const DatasetInfo*>& datasets) {
    std::vector<nlohmann::json> events;
    PerfettoContext context{&events, false};
    for (const DatasetInfo* dataset : datasets) {
        const auto status = visit_dataset(reader, options, dataset->channel_id, perfetto_visitor,
                                          &context);
        if (!status.has_value()) {
            return 1;
        }
    }
    nlohmann::json document{};
    document["traceEvents"] = events;
    document["displayTimeUnit"] = "ns";
    if (options.output.empty()) {
        std::fprintf(stderr, "nexigen-export: perfetto output requires --output\n");
        return 2;
    }
    std::ofstream file(options.output, std::ios::trunc);
    if (!file) {
        std::fprintf(stderr, "nexigen-export: cannot open output: %s\n", options.output.c_str());
        return 1;
    }
    file << document.dump(2) << '\n';
    return 0;
}

struct VideoContext {
    const SessionReader* reader{nullptr};
    std::ostream* stream{nullptr};
    std::ostream* sidecar{nullptr};
    bool failed{false};
};

bool video_visitor(void* context, const RecordHeader& header, std::span<const std::byte> payload,
                   std::uint64_t /*offset*/) {
    if (header.type != static_cast<std::uint16_t>(RecordType::VideoFrameRef) ||
        payload.size() != sizeof(VideoFrameRef)) {
        return true;
    }
    auto& state = *static_cast<VideoContext*>(context);
    VideoFrameRef reference{};
    std::memcpy(&reference, payload.data(), sizeof(reference));
    const auto bytes = state.reader->read_video(reference);
    if (!bytes.has_value()) {
        state.failed = true;
        return false;
    }
    state.stream->write(reinterpret_cast<const char*>(bytes->data()),
                        static_cast<std::streamsize>(bytes->size()));
    if (!*state.stream) {
        state.failed = true;
        return false;
    }
    nlohmann::json line{};
    line["offset"] = reference.vid_offset;
    line["size"] = reference.vid_size;
    line["capture_mono_ns"] = reference.capture_mono_ns;
    line["source_sequence"] = reference.source_sequence;
    line["config_epoch"] = reference.config_epoch;
    line["codec"] = reference.codec;
    line["bitstream"] = reference.bitstream;
    (*state.sidecar) << line.dump() << '\n';
    if (!*state.sidecar) {
        state.failed = true;
        return false;
    }
    return true;
}

int run_inspect(const SessionReader& reader, const Options& options) {
    const auto& manifest = reader.manifest();
    if (options.json) {
        const nlohmann::json document = manifest;
        std::cout << document.dump(2) << '\n';
        return 0;
    }
    std::printf("format=%s version=%u session=%s domain=%s clean=%s segments=%zu schemas=%zu "
                "channels=%zu\n",
                std::string(nexigen::recorder::to_string(manifest.format)).c_str(),
                manifest.version, manifest.session_id.c_str(),
                std::string(nexigen::recorder::to_string(manifest.time_domain)).c_str(),
                manifest.clean_close ? "true" : "false", manifest.segments.size(),
                manifest.schemas.size(), manifest.channels.size());
    for (const auto& segment : manifest.segments) {
        std::printf("  segment seq=%u rec=%s idx=%s vid=%s closed=%s records=%llu gaps=%llu "
                    "rec_bytes=%llu vid_bytes=%llu\n",
                    segment.seq, segment.rec_path.c_str(), segment.idx_path.c_str(),
                    segment.vid_path.c_str(), segment.closed ? "true" : "false",
                    static_cast<unsigned long long>(segment.records),
                    static_cast<unsigned long long>(segment.gaps),
                    static_cast<unsigned long long>(segment.rec_bytes),
                    static_cast<unsigned long long>(segment.vid_bytes));
    }
    for (const auto& schema : manifest.schemas) {
        std::printf("  schema id=%u name=%s encoding=%s bytes=%zu\n", schema.id,
                    schema.name.c_str(), schema.encoding.c_str(), schema.data.size());
    }
    for (const auto& channel : manifest.channels) {
        std::printf("  channel id=%u topic=%s kind=%s schema=%u message_encoding=%s\n",
                    channel.id, channel.topic.c_str(),
                    std::string(nexigen::recorder::to_string(channel.kind)).c_str(),
                    channel.schema_id, channel.message_encoding.c_str());
    }
    const auto datasets = reader.datasets();
    if (datasets.has_value()) {
        for (const auto& dataset : *datasets) {
            std::printf("  dataset id=%u kind=%s codec=%s bitstream=%s records=%llu gaps=%llu "
                        "closed=%s\n",
                        dataset.channel_id,
                        std::string(nexigen::recorder::to_string(dataset.kind)).c_str(),
                        std::string(nexigen::recorder::to_string(dataset.codec)).c_str(),
                        std::string(nexigen::recorder::to_string(dataset.bitstream)).c_str(),
                        static_cast<unsigned long long>(dataset.records),
                        static_cast<unsigned long long>(dataset.gaps),
                        dataset.closed ? "true" : "false");
        }
    }
    return 0;
}

int run_list(const SessionReader& reader, const Options& options) {
    const auto datasets = reader.datasets();
    if (!datasets.has_value()) {
        std::fprintf(stderr, "nexigen-export: cannot build dataset inventory\n");
        return 1;
    }
    if (options.json) {
        nlohmann::json array = nlohmann::json::array();
        for (const auto& dataset : *datasets) {
            array.push_back(dataset);
        }
        std::cout << array.dump(2) << '\n';
        return 0;
    }
    for (const auto& dataset : *datasets) {
        std::printf("id=%u kind=%s name=%s schema=%s codec=%s records=%llu gaps=%llu bytes=%llu "
                    "event=[%llu, %llu] segments=%zu closed=%s\n",
                    dataset.channel_id,
                    std::string(nexigen::recorder::to_string(dataset.kind)).c_str(),
                    dataset.name.c_str(), dataset.schema_name.c_str(),
                    std::string(nexigen::recorder::to_string(dataset.codec)).c_str(),
                    static_cast<unsigned long long>(dataset.records),
                    static_cast<unsigned long long>(dataset.gaps),
                    static_cast<unsigned long long>(dataset.payload_bytes),
                    static_cast<unsigned long long>(dataset.first_event_ns),
                    static_cast<unsigned long long>(dataset.last_event_ns),
                    dataset.segments.size(), dataset.closed ? "true" : "false");
    }
    return 0;
}

int run_info(const SessionReader& reader, const Options& options) {
    const auto datasets = reader.datasets();
    if (!datasets.has_value()) {
        std::fprintf(stderr, "nexigen-export: cannot build dataset inventory\n");
        return 1;
    }
    const DatasetInfo* dataset = find_dataset(*datasets, options.selection.dataset);
    if (dataset == nullptr) {
        std::fprintf(stderr, "nexigen-export: dataset not found: %s\n",
                     options.selection.dataset.c_str());
        print_available(*datasets);
        return 1;
    }
    if (options.json) {
        const nlohmann::json j = *dataset;
        std::cout << j.dump(2) << '\n';
        return 0;
    }
    std::printf("%s\n", dataset->to_string().c_str());
    for (const auto& field : dataset->fields) {
        std::printf("  field %s\n", field.to_string().c_str());
    }
    for (const auto& segment : dataset->segments) {
        std::printf("  %s\n", segment.to_string().c_str());
    }
    return 0;
}

/// @brief Resolve the requested datasets; returns the process exit code on failure.
[[nodiscard]] int resolve_datasets(const std::vector<DatasetInfo>& datasets,
                                   const Options& options,
                                   std::vector<const DatasetInfo*>& out) {
    std::vector<std::string> requested;
    if (!options.selection.dataset.empty()) {
        requested.push_back(options.selection.dataset);
    }
    for (const auto& channel : options.selection.channels) {
        requested.push_back(channel);
    }
    if (requested.empty()) {
        std::fprintf(stderr, "nexigen-export: no dataset selected; use --dataset or --channels\n");
        print_available(datasets);
        return 2;
    }
    for (const auto& selection : requested) {
        const DatasetInfo* dataset = find_dataset(datasets, selection);
        if (dataset == nullptr) {
            std::fprintf(stderr, "nexigen-export: dataset not found: %s\n", selection.c_str());
            print_available(datasets);
            return 1;
        }
        if (std::find(out.begin(), out.end(), dataset) == out.end()) {
            out.push_back(dataset);
        }
    }
    return 0;
}

[[nodiscard]] nexigen::core::Status visit_dataset(const SessionReader& reader,
                                                 const Options& options,
                                                 std::uint32_t channel_id,
                                                 nexigen::recorder::RecordVisitor visitor,
                                                 void* context) {
    const auto& selection = options.selection;
    const std::uint64_t start = selection.has_start ? selection.start_event_ns : 0U;
    const std::uint64_t end = selection.has_end
                                  ? selection.end_event_ns
                                  : std::numeric_limits<std::uint64_t>::max();
    return reader.for_each(channel_id, true, start, end,
                           selection.has_start || selection.has_end, visitor, context);
}

#if defined(NEXIGEN_HAS_MCAP)
int export_mcap(const SessionReader& reader, const Options& options,
                const std::vector<const DatasetInfo*>& datasets) {
    if (options.output.empty()) {
        std::fprintf(stderr, "nexigen-export: MCAP output requires --output\n");
        return 1;
    }
    auto mcap = nexigen::exportc::McapOutput::create(options.output);
    if (!mcap.has_value()) {
        std::fprintf(stderr, "nexigen-export: cannot create MCAP output\n");
        return 1;
    }
    const TimeSyncState sync = read_time_sync(reader);
    for (const DatasetInfo* dataset : datasets) {
        if (dataset == nullptr) {
            continue;
        }
        const auto* schema = find_schema(reader, dataset->schema_id);
        const bool opaque = dataset->kind == nexigen::recorder::DatasetKind::Opaque;
        std::string schema_data{};
        if (schema != nullptr) {
            schema_data.assign(reinterpret_cast<const char*>(schema->data.data()),
                               schema->data.size());
        }
        const std::string schema_name =
            dataset->schema_name.empty() ? std::string("nexigen.export.Record")
                                         : dataset->schema_name;
        const std::string schema_encoding =
            opaque ? dataset->schema_encoding : std::string("jsonschema");
        const std::string message_encoding =
            opaque ? dataset->message_encoding : std::string("json");
        const auto channel = mcap->add_channel(dataset->name, schema_name, schema_encoding,
                                               schema_data, message_encoding);
        if (!channel.has_value()) {
            std::fprintf(stderr, "nexigen-export: cannot add MCAP channel %u\n",
                         dataset->channel_id);
            return 1;
        }
        McapContext context{};
        context.output = &*mcap;
        context.channel_id = *channel;
        context.opaque = opaque;
        context.has_utc = sync.found;
        context.utc_offset_ns = sync.offset_ns;
        const auto status = visit_dataset(reader, options, dataset->channel_id, mcap_visitor,
                                          &context);
        if (!status.has_value() || context.failed) {
            std::fprintf(stderr, "nexigen-export: MCAP export failed for channel %u\n",
                         dataset->channel_id);
            return 1;
        }
    }
    if (!mcap->close()) {
        std::fprintf(stderr, "nexigen-export: MCAP close failed\n");
        return 1;
    }
    const auto verified = nexigen::exportc::McapOutput::verify(options.output);
    if (!verified.has_value() || *verified == 0U) {
        std::fprintf(stderr, "nexigen-export: MCAP verification failed\n");
        return 1;
    }
    return 0;
}
#endif

int export_elementary(const SessionReader& reader, const Options& options,
                      const std::vector<const DatasetInfo*>& datasets) {
    if (datasets.size() != 1U) {
        std::fprintf(stderr,
                     "nexigen-export: elementary output requires exactly one video dataset\n");
        return 2;
    }
    const DatasetInfo& dataset = *datasets.front();
    if (dataset.kind != nexigen::recorder::DatasetKind::Video) {
        std::fprintf(stderr, "nexigen-export: elementary output requires a video dataset\n");
        return 1;
    }
    if (dataset.codec == nexigen::recorder::VideoCodec::Unknown ||
        dataset.bitstream == nexigen::recorder::BitstreamFormat::Unknown) {
        std::fprintf(stderr,
                     "nexigen-export: video dataset %u (%s) is missing codec configuration\n",
                     dataset.channel_id, dataset.name.c_str());
        return 1;
    }
    const bool needs_extradata =
        (dataset.codec == nexigen::recorder::VideoCodec::H264 ||
         dataset.codec == nexigen::recorder::VideoCodec::H265) &&
        dataset.bitstream != nexigen::recorder::BitstreamFormat::AnnexB;
    if (needs_extradata && dataset.extradata_size == 0U) {
        std::fprintf(stderr,
                     "nexigen-export: video dataset %u (%s) is missing codec extradata\n",
                     dataset.channel_id, dataset.name.c_str());
        return 1;
    }
    if (options.output.empty()) {
        std::fprintf(stderr, "nexigen-export: elementary output requires --output\n");
        return 2;
    }
    std::ofstream stream(options.output, std::ios::binary | std::ios::trunc);
    std::ofstream sidecar(options.output + ".timestamps.jsonl");
    if (!stream || !sidecar) {
        std::fprintf(stderr, "nexigen-export: cannot open video output\n");
        return 1;
    }
    VideoContext context{&reader, &stream, &sidecar, false};
    const auto status = visit_dataset(reader, options, dataset.channel_id, video_visitor,
                                      &context);
    if (!status.has_value()) {
        std::fprintf(stderr, "nexigen-export: video extraction failed: %s\n",
                     std::string(nexigen::core::to_string(status.error())).c_str());
        return 1;
    }
    if (context.failed) {
        std::fprintf(stderr, "nexigen-export: video extraction failed\n");
        return 1;
    }
    return 0;
}

int export_jsonl(const SessionReader& reader, const Options& options,
                 const std::vector<const DatasetInfo*>& datasets) {
    if (options.output.empty()) {
        std::fprintf(stderr, "nexigen-export: jsonl output requires --output\n");
        return 2;
    }
    std::ofstream file(options.output, std::ios::trunc);
    if (!file) {
        std::fprintf(stderr, "nexigen-export: cannot open output: %s\n", options.output.c_str());
        return 1;
    }
    std::ostream* output = &file;
    for (const DatasetInfo* dataset : datasets) {
        JsonlContext context{};
        context.output = output;
        context.fields = &dataset->fields;
        context.encoding = dataset->schema_encoding;
        if (const auto* schema = find_schema(reader, dataset->schema_id); schema != nullptr) {
            context.descriptor = schema->data;
        }
        const auto status = visit_dataset(reader, options, dataset->channel_id, jsonl_visitor,
                                          &context);
        if (!status.has_value() || context.failed) {
            std::fprintf(stderr, "nexigen-export: extraction failed for channel %u\n",
                         dataset->channel_id);
            return 1;
        }
    }
    output->flush();
    return 0;
}

int run_extract(const SessionReader& reader, const Options& options) {
    const auto inventory = reader.datasets();
    if (!inventory.has_value()) {
        std::fprintf(stderr, "nexigen-export: cannot build dataset inventory\n");
        return 1;
    }
    std::vector<const DatasetInfo*> datasets;
    const int resolved = resolve_datasets(*inventory, options, datasets);
    if (resolved != 0) {
        return resolved;
    }

    if (options.format == "mp4" || options.format == "mkv") {
        std::fprintf(stderr,
                     "nexigen-export: UnsupportedFormat: %s remux requires the nexigen_media "
                     "follow-up; use --format elementary\n",
                     options.format.c_str());
        return 3;
    }
    if (options.format == "mcap") {
#if defined(NEXIGEN_HAS_MCAP)
        return export_mcap(reader, options, datasets);
#else
        std::fprintf(stderr,
                     "nexigen-export: MCAP output is not enabled in this build "
                     "(NEXIGEN_ENABLE_MCAP=OFF)\n");
        return 1;
#endif
    }
    if (options.format == "perfetto") {
        return run_perfetto(reader, options, datasets);
    }
    if (options.format == "elementary") {
        return export_elementary(reader, options, datasets);
    }
    if (options.format != "jsonl") {
        std::fprintf(stderr, "nexigen-export: UnsupportedFormat: %s\n",
                     options.format.c_str());
        return 3;
    }
    return export_jsonl(reader, options, datasets);
}

int run_verify(const SessionReader& reader) {
    std::uint64_t records = 0;
    std::uint64_t gaps = 0;
    nexigen::recorder::VerifyFailure failure{};
    const auto status = reader.verify(records, gaps, &failure);
    if (!status.has_value()) {
        std::fprintf(stderr,
                     "nexigen-export: verification failed: %s segment=%u offset=%llu channel=%u\n",
                     std::string(nexigen::core::to_string(status.error())).c_str(),
                     failure.segment_seq, static_cast<unsigned long long>(failure.offset),
                     failure.channel_id);
        return 1;
    }
    std::printf("records=%llu gaps=%llu clean=%s\n",
                static_cast<unsigned long long>(records),
                static_cast<unsigned long long>(gaps),
                reader.manifest().clean_close ? "true" : "false");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options options{};
    std::string parse_error{};
    if (!parse_options(argc, argv, options, parse_error)) {
        std::fprintf(stderr, "nexigen-export: %s\n", parse_error.c_str());
        print_usage(stderr);
        return 2;
    }
    if (options.help) {
        print_usage(stdout);
        return 0;
    }
    if (options.command == "jsonl") {
        options.command = "extract";
        options.format = "jsonl";
    } else if (options.command == "perfetto") {
        options.command = "extract";
        options.format = "perfetto";
    } else if (options.command == "video") {
        options.command = "extract";
        options.format = "elementary";
    }
    const auto reader = SessionReader::open(options.session);
    if (!reader.has_value()) {
        std::fprintf(stderr, "nexigen-export: cannot open session: %s\n", options.session.c_str());
        return 1;
    }
    if (options.command == "inspect") {
        return run_inspect(*reader, options);
    }
    if (options.command == "list") {
        return run_list(*reader, options);
    }
    if (options.command == "info") {
        return run_info(*reader, options);
    }
    if (options.command == "verify") {
        return run_verify(*reader);
    }
    if (options.command == "extract") {
        return run_extract(*reader, options);
    }
    print_usage(stderr);
    return 2;
}
