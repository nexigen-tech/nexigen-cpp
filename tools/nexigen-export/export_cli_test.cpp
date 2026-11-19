// nexigen-export CLI regression suite (change add-recorder-raw-format).
//
// Writes a real raw session with the recorder library, then drives the actual
// `nexigen-export` binary as a child process and checks every subcommand's exit
// code and output: help/usage errors, list/inspect/info, JSONL (schema-driven
// and opaque), event-time ranges, multi-channel selection, Perfetto, elementary
// video + timestamp sidecar, format rejections (MP4/MKV/unknown), verify
// diagnostics (truncated tail and corrupted frame CRC), relocated-session
// decoding, and MCAP (enabled or explicitly rejected).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/recorder/recorder_types.hpp>
#include <nexigen/recorder/session_reader.hpp>
#include <nexigen/recorder/session_writer.hpp>
#include <nexigen/recorder/wire_types.hpp>
#include <nexigen/tracing/span_frame.hpp>

namespace {

int g_checks = 0;
int g_failures = 0;

std::string g_exporter;
std::string g_root;
std::string g_session;
bool g_mcap_enabled = false;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

#define NX_CHECK_RUN(label, result, ...)                                                           \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d [%s] exit=%d\n  stdout=%s\n  stderr=%s\n", __FILE__,  \
                         __LINE__, label, (result).code, (result).out.c_str(),                     \
                         (result).err.c_str());                                                    \
        }                                                                                          \
    } while (false)

[[nodiscard]] std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end <= 0) {
        return {};
    }
    std::string text(static_cast<std::size_t>(end), '\0');
    input.seekg(0, std::ios::beg);
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    return text;
}

[[nodiscard]] std::uint64_t file_size(const std::string& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0U : static_cast<std::uint64_t>(size);
}

[[nodiscard]] std::string temp_file(const char* tag) {
    std::string pattern = g_root + "/" + tag + "-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const int fd = ::mkstemp(buffer.data());
    if (fd < 0) {
        return {};
    }
    static_cast<void>(::close(fd));
    return std::string(buffer.data());
}

struct Run {
    int code{-1};
    std::string out{};
    std::string err{};
};

/// @brief Run the converter with `args`, capturing merged output streams.
[[nodiscard]] Run run_exporter(const std::vector<std::string>& args) {
    const std::string out_path = temp_file("out");
    const std::string err_path = temp_file("err");
    Run result{};
    if (out_path.empty() || err_path.empty()) {
        return result;
    }
    std::vector<std::string> full{};
    full.reserve(args.size() + 1U);
    full.push_back(g_exporter);
    full.insert(full.end(), args.begin(), args.end());
    std::vector<char*> argv{};
    argv.reserve(full.size() + 1U);
    for (const auto& arg : full) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t child = ::fork();
    if (child == 0) {
        const int out_fd = ::open(out_path.c_str(), O_WRONLY | O_TRUNC);
        const int err_fd = ::open(err_path.c_str(), O_WRONLY | O_TRUNC);
        if (out_fd >= 0) {
            static_cast<void>(::dup2(out_fd, STDOUT_FILENO));
        }
        if (err_fd >= 0) {
            static_cast<void>(::dup2(err_fd, STDERR_FILENO));
        }
        ::execv(g_exporter.c_str(), argv.data());
        ::_exit(127);
    }
    if (child < 0) {
        return result;
    }
    int status = 0;
    if (::waitpid(child, &status, 0) == child && WIFEXITED(status)) {
        result.code = WEXITSTATUS(status);
    }
    result.out = read_file(out_path);
    result.err = read_file(err_path);
    std::filesystem::remove(out_path);
    std::filesystem::remove(err_path);
    return result;
}

[[nodiscard]] nlohmann::json parse_json(const std::string& text) {
    return nlohmann::json::parse(text, nullptr, false);
}

[[nodiscard]] std::vector<nlohmann::json> parse_lines(const std::string& text) {
    std::vector<nlohmann::json> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto newline = text.find('\n', start);
        const auto length = newline == std::string::npos ? text.size() - start : newline - start;
        const std::string line = text.substr(start, length);
        if (!line.empty()) {
            auto parsed = parse_json(line);
            if (!parsed.is_discarded()) {
                lines.push_back(std::move(parsed));
            }
        }
        if (newline == std::string::npos) {
            break;
        }
        start = newline + 1U;
    }
    return lines;
}

[[nodiscard]] const nlohmann::json* find_entry(const nlohmann::json& array,
                                               std::uint32_t channel_id) {
    if (!array.is_array()) {
        return nullptr;
    }
    for (const auto& entry : array) {
        if (entry.is_object() && entry.value("channel_id", 0U) == channel_id) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] bool copy_tree(const std::string& from, const std::string& to) {
    std::error_code error;
    std::filesystem::remove_all(to, error);
    std::filesystem::copy(from, to,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          error);
    return !error;
}

[[nodiscard]] bool write_json(const std::string& path, const nlohmann::json& value) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << value.dump(2) << '\n';
    return static_cast<bool>(out);
}

[[nodiscard]] bool mcap_contains_u64(const std::string& bytes, std::uint64_t value) {
    std::string needle(sizeof(value), '\0');
    std::memcpy(needle.data(), &value, sizeof(value));
    return bytes.find(needle) != std::string::npos;
}

// Fixture --------------------------------------------------------------------

[[nodiscard]] std::vector<std::uint8_t> json_bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

void put_u64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t i = 0; i < 8U; ++i) {
        const auto shift = static_cast<unsigned>(i * 8U);
        const auto part = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
        bytes[offset + i] = static_cast<std::byte>(part);
    }
}

[[nodiscard]] std::vector<std::byte> make_frame(std::size_t size, std::uint8_t seed) {
    std::vector<std::byte> frame(size);
    for (std::size_t i = 0; i < size; ++i) {
        const auto value = static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(i));
        frame[i] = static_cast<std::byte>(value);
    }
    return frame;
}

struct FixtureChannels {
    std::uint32_t telemetry{0};
    std::uint32_t video{0};
    std::uint32_t opaque{0};
    std::uint32_t trace{0};
};

[[nodiscard]] bool declare_schemas(nexigen::recorder::SessionWriter& writer) {
    nexigen::recorder::SchemaDeclare log{};
    log.id = 1;
    log.name = "nexigen.log.LogFrame";
    log.encoding = "jsonschema";
    log.data = json_bytes(
        R"({"type":"object","fields":[{"name":"value","type":"u64","offset":0,"element_size":8,"count":1,"stride":8}]})");
    if (!writer.declare_schema(log).has_value()) {
        return false;
    }
    nexigen::recorder::SchemaDeclare video{};
    video.id = 2;
    video.name = "nexigen.video.FrameRef";
    video.encoding = "jsonschema";
    video.data = json_bytes(R"({"type":"object","fields":[]})");
    if (!writer.declare_schema(video).has_value()) {
        return false;
    }
    nexigen::recorder::SchemaDeclare opaque{};
    opaque.id = 3;
    opaque.name = "test.Opaque";
    opaque.encoding = "protobuf";
    const std::array<std::uint8_t, 14> descriptor{0x0AU, 0x0CU, 0x0AU, 0x0AU, 't', 'e', 's',
                                                  't',   '.',   'p',   'r',   'o', 't', 'o'};
    opaque.data.assign(descriptor.begin(), descriptor.end());
    if (!writer.declare_schema(opaque).has_value()) {
        return false;
    }
    nexigen::recorder::SchemaDeclare trace{};
    trace.id = 4;
    trace.name = "nexigen.trace.SpanFrame";
    trace.encoding = "jsonschema";
    trace.data = json_bytes(R"({"type":"object","fields":[]})");
    return writer.declare_schema(trace).has_value();
}

[[nodiscard]] bool declare_channels(nexigen::recorder::SessionWriter& writer,
                                    FixtureChannels& ids) {
    const auto telemetry = writer.declare_channel(
        "test/log", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    if (!telemetry.has_value()) {
        return false;
    }
    ids.telemetry = *telemetry;
    const auto video = writer.declare_channel("test/camera", 2,
                                              nexigen::recorder::DatasetKind::Video, "nexigen.pod");
    if (!video.has_value()) {
        return false;
    }
    ids.video = *video;
    const auto opaque = writer.declare_channel("test/opaque", 3,
                                               nexigen::recorder::DatasetKind::Opaque, "protobuf");
    if (!opaque.has_value()) {
        return false;
    }
    ids.opaque = *opaque;
    const auto trace = writer.declare_channel(
        "test/trace", 4, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    if (!trace.has_value()) {
        return false;
    }
    ids.trace = *trace;
    return true;
}

[[nodiscard]] bool append_video_frames(nexigen::recorder::SessionWriter& writer,
                                       std::uint32_t channel_id) {
    nexigen::recorder::VideoStreamConfig config{};
    config.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H265);
    config.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::AnnexB);
    config.width = 1920;
    config.height = 1080;
    config.config_epoch = 1;
    config.extradata_size = 3;
    config.extradata[0] = static_cast<std::uint8_t>('x');
    config.extradata[1] = static_cast<std::uint8_t>('y');
    config.extradata[2] = static_cast<std::uint8_t>('z');
    struct FrameSpec {
        std::uint64_t capture_ns;
        std::uint64_t sequence;
        std::size_t size;
        std::uint8_t seed;
    };
    const std::array<FrameSpec, 2> specs{
        {{1'000'000ULL, 0ULL, 256U, 0x10}, {21'000'000ULL, 1ULL, 400U, 0x90}}};
    for (const auto& spec : specs) {
        const auto frame = make_frame(spec.size, spec.seed);
        nexigen::recorder::VideoFrameRef reference{};
        reference.capture_mono_ns = spec.capture_ns;
        reference.source_sequence = spec.sequence;
        reference.config_epoch = 1;
        if (!writer.append_video(channel_id, config, reference, frame).has_value()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool append_records(nexigen::recorder::SessionWriter& writer,
                                  const FixtureChannels& ids) {
    if (!writer.append_time_sync(1'700'000'000'000'000'000LL).has_value()) {
        return false;
    }
    for (const std::uint64_t value : {10ULL, 20ULL, 30ULL}) {
        std::vector<std::byte> payload(16, std::byte{0});
        put_u64(payload, 0, value);
        const std::uint64_t sequence = value / 10U - 1U;
        if (!writer
                 .append(nexigen::recorder::RecordType::ComPayload, ids.telemetry, payload, value,
                         sequence, 1)
                 .has_value()) {
            return false;
        }
    }
    if (!append_video_frames(writer, ids.video)) {
        return false;
    }
    const std::vector<std::byte> opaque_payload{std::byte{0x08}, std::byte{0x2A}, std::byte{0x00}};
    for (std::uint64_t sequence = 0; sequence < 2U; ++sequence) {
        if (!writer
                 .append(nexigen::recorder::RecordType::ComPayload, ids.opaque, opaque_payload,
                         sequence, sequence, 1)
                 .has_value()) {
            return false;
        }
    }
    nexigen::tracing::SpanFrame span{};
    span.start_ns = 5'000'000;
    span.end_ns = 8'000'000;
    span.pid = 42;
    span.thread_id = 7;
    std::snprintf(span.name, sizeof(span.name), "%s", "export.span");
    const auto span_bytes =
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(&span), sizeof(span));
    if (!writer
             .append(nexigen::recorder::RecordType::SpanFrame, ids.trace, span_bytes, span.start_ns,
                     0, 0)
             .has_value()) {
        return false;
    }
    return writer.close().has_value();
}

[[nodiscard]] bool write_fixture() {
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = g_root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U << 20U;
    settings.max_segment_records = 0;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 1024;
    settings.video_frame_buffer_bytes = 8192;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = 0;
    settings.video_crc = nexigen::recorder::VideoCrcMode::PerFrame;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    if (!writer.has_value()) {
        return false;
    }
    if (!declare_schemas(*writer)) {
        return false;
    }
    FixtureChannels ids{};
    if (!declare_channels(*writer, ids)) {
        return false;
    }
    if (ids.telemetry != 1U || ids.video != 2U || ids.opaque != 3U || ids.trace != 4U) {
        return false;
    }
    return append_records(*writer, ids);
}

/// @brief UTC epoch base used by the time-base fixture rows.
constexpr std::uint64_t kFixtureRealtimeBase = 1'700'000'000'000'000'000ULL;

/// @brief Write a hardware-domain session with time-base history and snapshots.
///
/// @param root Session root; the session id is `sess`.
/// @param append_legacy Also append a retired-layout payload, as an old runtime
///        would have written.
/// @return `true` when the session closed cleanly.
[[nodiscard]] bool write_time_base_fixture(const std::string& root, bool append_legacy,
                                           bool unresolved = false) {
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.max_segment_bytes = 1U << 20U;
    settings.write_buffer_bytes = 8192;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 4096;
    settings.max_record_bytes = 8192;
    settings.min_free_bytes = 0;
    settings.boot_token = "0123456789abcdef";
    settings.boot_uuid = "01234567-89ab-cdef-0123-456789abcdef";
    settings.time_base_epoch = 42;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    if (!writer.has_value()) {
        return false;
    }

    nexigen::recorder::SchemaDeclare ticks{};
    ticks.id = 10;
    ticks.name = "test.FpgaTicks";
    ticks.encoding = "jsonschema";
    ticks.data = json_bytes(
        R"({"type":"object","fields":[{"name":"native_ticks","type":"u64","offset":0,"element_size":8,"count":1,"stride":8}]})");
    if (!writer->declare_schema(ticks).has_value()) {
        return false;
    }
    nexigen::recorder::ChannelDeclare channel{};
    channel.id = 1;
    channel.schema_id = 10;
    channel.topic = "fpga/events";
    channel.kind = nexigen::recorder::DatasetKind::Telemetry;
    channel.message_encoding = "nexigen.pod";
    channel.native_domain = "device";
    channel.domain_id = 7;
    channel.domain_epoch = 5;
    if (!writer->declare_channel(channel).has_value()) {
        return false;
    }
    nexigen::recorder::ChannelDeclare meta = channel;
    meta.id = 2;
    meta.topic = "fpga/meta";
    if (!writer->declare_channel(meta).has_value()) {
        return false;
    }

    nexigen::recorder::TimeBasePayload entry{};
    entry.generation = 7U;
    entry.time_base_epoch = 42U;
    entry.boot_token = 0x0123456789abcdefULL;
    entry.status = nexigen::chrono::TimeBaseStatus::Ready;
    entry.sync_mode = nexigen::chrono::SyncMode::PtpSynced;
    entry.service_flags =
        nexigen::chrono::ServiceFlag::PhcPresent | nexigen::chrono::ServiceFlag::TaiSynchronized;
    entry.tai_offset_ns = 37'000'000'000LL;
    entry.utc_unc_ns = 250U;
    entry.domain_count = 3U;
    std::memcpy(entry.boot_uuid.data(), "01234567-89ab-cdef-0123-456789abcdef", 36);
    entry.rows[0].domain_id = 7U;
    entry.rows[0].domain_class = nexigen::chrono::TimeDomainClass::DeviceClock;
    entry.rows[0].lock = nexigen::chrono::LockState::Locked;
    entry.rows[0].epoch = 5U;
    entry.rows[0].native_anchor = 1'000U;
    entry.rows[0].canonical_anchor_ns = 2'000;
    entry.rows[0].scale_fwd_q32 = 1ULL << 32U;
    entry.rows[0].scale_rev_q32 = 1ULL << 32U;
    entry.rows[0].valid_from_native = 0U;
    entry.rows[0].valid_to_native = unresolved ? 5'000U : 10'000U;
    entry.rows[0].valid_from_canonical = 1'000U;
    entry.rows[0].valid_to_canonical = unresolved ? 6'000U : 11'000U;
    entry.rows[0].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;
    entry.rows[1].domain_id = nexigen::chrono::k_realtime_domain_id;
    entry.rows[1].domain_class = nexigen::chrono::TimeDomainClass::SystemClock;
    entry.rows[1].lock = nexigen::chrono::LockState::Locked;
    entry.rows[1].epoch = 1U;
    entry.rows[1].native_anchor = kFixtureRealtimeBase;
    entry.rows[1].canonical_anchor_ns = 2'000;
    entry.rows[1].scale_fwd_q32 = 1ULL << 32U;
    entry.rows[1].scale_rev_q32 = 1ULL << 32U;
    entry.rows[1].valid_from_canonical = 1'000U;
    entry.rows[1].valid_to_canonical = 11'000U;
    entry.rows[1].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;
    entry.rows[2].domain_id = nexigen::chrono::k_tai_domain_id;
    entry.rows[2].domain_class = nexigen::chrono::TimeDomainClass::SystemClock;
    entry.rows[2].lock = nexigen::chrono::LockState::Locked;
    entry.rows[2].epoch = 1U;
    entry.rows[2].native_anchor = kFixtureRealtimeBase + 37'000'000'000ULL;
    entry.rows[2].canonical_anchor_ns = 2'000;
    entry.rows[2].scale_fwd_q32 = 1ULL << 32U;
    entry.rows[2].scale_rev_q32 = 1ULL << 32U;
    entry.rows[2].valid_from_canonical = 1'000U;
    entry.rows[2].valid_to_canonical = 11'000U;
    entry.rows[2].flags = nexigen::chrono::DomainCoefficient::has_reverse_bit;
    if (!writer->append_time_base(entry).has_value()) {
        return false;
    }

    nexigen::chrono::TimeSnapshot snapshot{};
    snapshot.generation = 7U;
    snapshot.boot_token = entry.boot_token;
    snapshot.status = nexigen::chrono::TimeBaseStatus::Ready;
    snapshot.sync_mode = nexigen::chrono::SyncMode::PtpSynced;
    snapshot.monotonic_raw_ns = unresolved ? 50'000U : 5'000U;
    snapshot.realtime_ns = kFixtureRealtimeBase + 3'000U;
    snapshot.tai_ns = kFixtureRealtimeBase + 37'000'000'000ULL + 3'000U;
    snapshot.domain_count = 1U;
    snapshot.domains[0].domain_id = 7U;
    snapshot.domains[0].domain_class = nexigen::chrono::TimeDomainClass::DeviceClock;
    snapshot.domains[0].lock = nexigen::chrono::LockState::Locked;
    snapshot.domains[0].epoch = 5U;
    snapshot.domains[0].value = 4'000U;
    if (!writer->append_time_snapshot(snapshot).has_value()) {
        return false;
    }

    if (append_legacy) {
        nexigen::recorder::TimeBasePayload legacy = entry;
        legacy.layout = 0U;
        const auto bytes =
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(&legacy), sizeof(legacy));
        if (!writer->append(nexigen::recorder::RecordType::TimeBase, 2, bytes, 5'000U, 0, 0)
                 .has_value()) {
            return false;
        }
    }

    std::vector<std::byte> payload(8, std::byte{0});
    const std::uint64_t native = unresolved ? 20'000U : 1'500U;
    put_u64(payload, 0, native);
    if (!writer->append(nexigen::recorder::RecordType::ComPayload, 1, payload, native, 0, 1)
             .has_value()) {
        return false;
    }
    return writer->close().has_value();
}

// Scenario checks ------------------------------------------------------------

void test_help_and_usage() {
    const auto help = run_exporter({"--help"});
    NX_CHECK_RUN("help", help, help.code == 0);
    NX_CHECK(help.out.find("commands: list, info, extract") != std::string::npos);
    NX_CHECK(help.out.find("--channels <list>") != std::string::npos);

    const auto empty = run_exporter({});
    NX_CHECK_RUN("no-args", empty, empty.code == 2);
    NX_CHECK(empty.err.find("missing command") != std::string::npos);
    NX_CHECK(empty.err.find("usage:") != std::string::npos);
    NX_CHECK(empty.out.empty());

    const auto bad_option = run_exporter({"list", g_session, "--nope"});
    NX_CHECK_RUN("bad-option", bad_option, bad_option.code == 2);
    NX_CHECK(bad_option.err.find("unknown argument: --nope") != std::string::npos);

    const auto bad_bound =
        run_exporter({"extract", g_session, "--dataset", "1", "--start", "not-a-number"});
    NX_CHECK_RUN("bad-bound", bad_bound, bad_bound.code == 2);
    NX_CHECK(bad_bound.err.find("--start: invalid value: not-a-number") != std::string::npos);

    const auto unknown_command = run_exporter({"explode", g_session});
    NX_CHECK_RUN("unknown-command", unknown_command, unknown_command.code == 2);
    NX_CHECK(unknown_command.err.find("unknown command: explode") != std::string::npos);

    const auto follow = run_exporter({"list", g_session, "--follow"});
    NX_CHECK_RUN("follow-rejected", follow, follow.code == 2);
    NX_CHECK(follow.err.find("on demand") != std::string::npos);

    const auto late_help = run_exporter({"list", g_session, "--help"});
    NX_CHECK_RUN("late-help", late_help, late_help.code == 0);
    NX_CHECK(late_help.out.find("usage:") != std::string::npos);
}

void test_list_and_inspect() {
    const auto list = run_exporter({"list", g_session});
    NX_CHECK_RUN("list", list, list.code == 0);
    NX_CHECK(list.out.find("id=1 kind=Telemetry name=test/log") != std::string::npos);
    NX_CHECK(list.out.find("id=2 kind=Video name=test/camera") != std::string::npos);
    NX_CHECK(list.out.find("id=3 kind=Opaque name=test/opaque") != std::string::npos);
    NX_CHECK(list.out.find("codec=H265") != std::string::npos);
    NX_CHECK(list.out.find("closed=true") != std::string::npos);
    NX_CHECK(list.out.find("segments=1") != std::string::npos);

    const auto list_json = run_exporter({"list", g_session, "--json"});
    NX_CHECK_RUN("list-json", list_json, list_json.code == 0);
    const auto array = parse_json(list_json.out);
    NX_CHECK(array.is_array());
    NX_CHECK(array.is_array() && array.size() == 4U);
    const auto* video = find_entry(array, 2U);
    NX_CHECK(video != nullptr);
    if (video != nullptr) {
        NX_CHECK((*video)["kind"] == "Video");
        NX_CHECK((*video)["schema_name"] == "nexigen.video.FrameRef");
        NX_CHECK((*video)["records"] == 2U);
        NX_CHECK((*video)["gaps"] == 0U);
        NX_CHECK((*video)["width"] == 1920U);
        NX_CHECK((*video)["height"] == 1080U);
        NX_CHECK((*video)["extradata_size"] == 3U);
        NX_CHECK((*video)["closed"] == true);
        NX_CHECK((*video)["config_epochs"].size() == 1U);
        NX_CHECK((*video)["config_epochs"][0] == 1U);
        NX_CHECK((*video)["first_event_ns"] == 1'000'000U);
        NX_CHECK((*video)["last_event_ns"] == 21'000'000U);
    }

    const auto inspect = run_exporter({"inspect", g_session});
    NX_CHECK_RUN("inspect", inspect, inspect.code == 0);
    NX_CHECK(inspect.out.find("format=Raw version=2 session=sess domain=MonotonicRaw "
                              "clean=true") != std::string::npos);
    NX_CHECK(inspect.out.find("dataset id=2 kind=Video codec=H265 bitstream=AnnexB") !=
             std::string::npos);

    const auto inspect_json = run_exporter({"inspect", g_session, "--json"});
    NX_CHECK_RUN("inspect-json", inspect_json, inspect_json.code == 0);
    const auto manifest = parse_json(inspect_json.out);
    NX_CHECK(manifest.is_object());
    NX_CHECK(manifest["format"] == "Raw");
    NX_CHECK(manifest["version"] == 2U);
    NX_CHECK(manifest["time_domain"] == "MonotonicRaw");
    NX_CHECK(manifest["clean_close"] == true);
    NX_CHECK(manifest["segments"].size() == 1U);
    NX_CHECK(manifest["schemas"].size() == 4U);
    NX_CHECK(manifest["channels"].size() == 4U);
}

void test_info() {
    const auto by_id = run_exporter({"info", g_session, "--dataset", "2", "--json"});
    NX_CHECK_RUN("info-id-json", by_id, by_id.code == 0);
    const auto info = parse_json(by_id.out);
    NX_CHECK(info.is_object());
    NX_CHECK(info["channel_id"] == 2U);
    NX_CHECK(info["name"] == "test/camera");
    NX_CHECK(info["codec"] == "H265");
    NX_CHECK(info["bitstream"] == "AnnexB");
    NX_CHECK(info["width"] == 1920U);
    NX_CHECK(info["height"] == 1080U);
    NX_CHECK(info["extradata_size"] == 3U);
    NX_CHECK(info["gaps"] == 0U);
    NX_CHECK(info["closed"] == true);
    NX_CHECK(info["segments"].size() == 1U);
    NX_CHECK(info["segments"][0]["closed"] == true);
    NX_CHECK(info["fields"].is_array());

    const auto by_topic = run_exporter({"info", g_session, "--dataset", "test/camera"});
    NX_CHECK_RUN("info-topic", by_topic, by_topic.code == 0);
    NX_CHECK(by_topic.out.find("DatasetInfo(id=2, name=test/camera") != std::string::npos);

    const auto unknown = run_exporter({"info", g_session, "--dataset", "nope"});
    NX_CHECK_RUN("info-unknown", unknown, unknown.code == 1);
    NX_CHECK(unknown.err.find("dataset not found: nope") != std::string::npos);
    NX_CHECK(unknown.err.find("available datasets:") != std::string::npos);
    NX_CHECK(unknown.err.find("1(test/log)") != std::string::npos);
}

void test_jsonl_and_ranges() {
    const std::string telemetry_path = g_root + "/telemetry.jsonl";
    const auto telemetry =
        run_exporter({"jsonl", g_session, "--dataset", "1", "--output", telemetry_path});
    NX_CHECK_RUN("jsonl-telemetry", telemetry, telemetry.code == 0);
    const auto lines = parse_lines(read_file(telemetry_path));
    NX_CHECK(lines.size() == 3U);
    if (lines.size() == 3U) {
        NX_CHECK(lines[0]["channel"] == 1U);
        NX_CHECK(lines[0]["type"] == 6U);
        NX_CHECK(lines[0]["encoding"] == "jsonschema");
        NX_CHECK(lines[0]["payload"]["value"] == 10U);
        NX_CHECK(lines[1]["payload"]["value"] == 20U);
        NX_CHECK(lines[2]["payload"]["value"] == 30U);
    }

    const std::string opaque_path = g_root + "/opaque.jsonl";
    const auto opaque =
        run_exporter({"jsonl", g_session, "--dataset", "3", "--output", opaque_path});
    NX_CHECK_RUN("jsonl-opaque", opaque, opaque.code == 0);
    const auto opaque_lines = parse_lines(read_file(opaque_path));
    NX_CHECK(opaque_lines.size() == 2U);
    if (opaque_lines.size() == 2U) {
        NX_CHECK(opaque_lines[0]["encoding"] == "protobuf");
        NX_CHECK(opaque_lines[0]["payload_base64"] == "CCoA");
    }

    const std::string ranged_path = g_root + "/ranged.jsonl";
    const auto ranged = run_exporter({"extract", g_session, "--dataset", "2", "--format", "jsonl",
                                      "--start", "20000000", "--output", ranged_path});
    NX_CHECK_RUN("jsonl-range", ranged, ranged.code == 0);
    const auto ranged_lines = parse_lines(read_file(ranged_path));
    // The stream configuration follows the first frame but carries its own
    // commit time, so the window keeps it together with the second frame.
    NX_CHECK(ranged_lines.size() == 2U);
    std::size_t frame_lines = 0;
    for (const auto& line : ranged_lines) {
        if (line["type"] == 7U) {
            ++frame_lines;
            NX_CHECK(line["payload"]["capture_mono_ns"] == 21'000'000U);
            NX_CHECK(line["payload"]["vid_size"] == 400U);
        }
    }
    NX_CHECK(frame_lines == 1U);

    const std::string combined_path = g_root + "/combined.jsonl";
    const auto combined = run_exporter({"extract", g_session, "--channels", "1,2", "--format",
                                        "jsonl", "--output", combined_path});
    NX_CHECK_RUN("jsonl-multi", combined, combined.code == 0);
    NX_CHECK(parse_lines(read_file(combined_path)).size() == 6U);

    const auto missing = run_exporter({"jsonl", g_session});
    NX_CHECK_RUN("jsonl-no-selection", missing, missing.code == 2);
    NX_CHECK(missing.err.find("no dataset selected") != std::string::npos);

    const auto no_output = run_exporter({"jsonl", g_session, "--dataset", "1"});
    NX_CHECK_RUN("jsonl-no-output", no_output, no_output.code == 2);
    NX_CHECK(no_output.err.find("--output") != std::string::npos);
}

void test_perfetto() {
    const std::string path = g_root + "/trace.json";
    const auto result = run_exporter({"perfetto", g_session, "--dataset", "4", "--output", path});
    NX_CHECK_RUN("perfetto", result, result.code == 0);
    const auto document = parse_json(read_file(path));
    NX_CHECK(document.is_object());
    NX_CHECK(document["displayTimeUnit"] == "ns");
    NX_CHECK(document["traceEvents"].is_array());
    NX_CHECK(document["traceEvents"].size() == 1U);
    if (document["traceEvents"].size() == 1U) {
        const auto& event = document["traceEvents"][0];
        NX_CHECK(event["ph"] == "X");
        NX_CHECK(event["name"] == "export.span");
        NX_CHECK(event["ts"] == 5'000U);
        NX_CHECK(event["dur"] == 3'000U);
        NX_CHECK(event["pid"] == 42U);
        NX_CHECK(event["tid"] == 7U);
    }
}

void test_video() {
    const std::string path = g_root + "/frames.h265";
    const auto result = run_exporter({"video", g_session, "--dataset", "2", "--output", path});
    NX_CHECK_RUN("video", result, result.code == 0);
    NX_CHECK(file_size(path) == 256U + 400U);
    std::vector<std::byte> expected = make_frame(256U, 0x10);
    const auto second = make_frame(400U, 0x90);
    expected.insert(expected.end(), second.begin(), second.end());
    const std::string bytes = read_file(path);
    NX_CHECK(bytes.size() == expected.size());
    NX_CHECK(bytes.size() == expected.size() &&
             std::memcmp(bytes.data(), expected.data(), expected.size()) == 0);

    const auto sidecar = parse_lines(read_file(path + ".timestamps.jsonl"));
    NX_CHECK(sidecar.size() == 2U);
    if (sidecar.size() == 2U) {
        NX_CHECK(sidecar[0]["capture_mono_ns"] == 1'000'000U);
        NX_CHECK(sidecar[0]["size"] == 256U);
        NX_CHECK(sidecar[1]["capture_mono_ns"] == 21'000'000U);
        NX_CHECK(sidecar[1]["size"] == 400U);
    }

    const std::string ranged_path = g_root + "/frames-range.h265";
    const auto ranged =
        run_exporter({"extract", g_session, "--dataset", "2", "--format", "elementary", "--start",
                      "20000000", "--output", ranged_path});
    NX_CHECK_RUN("video-range", ranged, ranged.code == 0);
    NX_CHECK(file_size(ranged_path) == 400U);

    const auto telemetry =
        run_exporter({"video", g_session, "--dataset", "1", "--output", g_root + "/not-video.bin"});
    NX_CHECK_RUN("video-non-video", telemetry, telemetry.code == 1);
    NX_CHECK(telemetry.err.find("requires a video dataset") != std::string::npos);
}

void test_format_rejections() {
    const std::string mp4 = g_root + "/out.mp4";
    const auto mp4_run =
        run_exporter({"extract", g_session, "--dataset", "2", "--format", "mp4", "--output", mp4});
    NX_CHECK_RUN("mp4", mp4_run, mp4_run.code == 3);
    NX_CHECK(mp4_run.err.find("UnsupportedFormat") != std::string::npos);
    NX_CHECK(mp4_run.err.find("nexigen_media") != std::string::npos);
    NX_CHECK(!std::filesystem::exists(mp4));

    const std::string mkv = g_root + "/out.mkv";
    const auto mkv_run =
        run_exporter({"extract", g_session, "--dataset", "2", "--format", "mkv", "--output", mkv});
    NX_CHECK_RUN("mkv", mkv_run, mkv_run.code == 3);
    NX_CHECK(!std::filesystem::exists(mkv));

    const auto bogus = run_exporter({"extract", g_session, "--dataset", "1", "--format", "bogus"});
    NX_CHECK_RUN("bogus-format", bogus, bogus.code == 3);
    NX_CHECK(bogus.err.find("UnsupportedFormat: bogus") != std::string::npos);

    const auto absent = run_exporter({"list", g_root + "/missing"});
    NX_CHECK_RUN("missing-session", absent, absent.code == 1);
    NX_CHECK(absent.err.find("cannot open session") != std::string::npos);
}

void test_verify() {
    const auto good = run_exporter({"verify", g_session});
    NX_CHECK_RUN("verify-good", good, good.code == 0);
    NX_CHECK(good.out.find("records=") != std::string::npos);
    NX_CHECK(good.out.find("gaps=0") != std::string::npos);
    NX_CHECK(good.out.find("clean=true") != std::string::npos);

    const std::string truncated_copy = g_root + "/truncated";
    NX_CHECK(copy_tree(g_session, truncated_copy));
    const std::string rec = truncated_copy + "/sess_000000.nxrec";
    std::error_code error;
    const auto size = std::filesystem::file_size(rec, error);
    NX_CHECK(!error && size > 20U);
    std::filesystem::resize_file(rec, size - 20U, error);
    NX_CHECK(!error);
    const auto truncated = run_exporter({"verify", truncated_copy});
    NX_CHECK_RUN("verify-truncated", truncated, truncated.code == 1);
    NX_CHECK(truncated.err.find("verification failed") != std::string::npos);
    NX_CHECK(truncated.err.find("segment=0") != std::string::npos);
    NX_CHECK(truncated.err.find("offset=") != std::string::npos);

    const std::string frame_copy = g_root + "/frame-corrupt";
    NX_CHECK(copy_tree(g_session, frame_copy));
    const std::string vid = frame_copy + "/sess_000000.nxvid";
    {
        std::fstream frame_file(vid, std::ios::in | std::ios::out | std::ios::binary);
        char first = 0;
        frame_file.read(&first, 1);
        const auto flipped = static_cast<char>(first ^ static_cast<char>(0x5A));
        frame_file.seekp(0);
        frame_file.write(&flipped, 1);
    }
    const auto corrupt_frame = run_exporter({"verify", frame_copy});
    NX_CHECK_RUN("verify-frame", corrupt_frame, corrupt_frame.code == 1);
    NX_CHECK(corrupt_frame.err.find("SecurityError") != std::string::npos);
    NX_CHECK(corrupt_frame.err.find("segment=0") != std::string::npos);
}

void test_relocated_session() {
    const std::string relocated = g_root + "/relocated";
    NX_CHECK(copy_tree(g_session, relocated));
    const auto list = run_exporter({"list", relocated});
    NX_CHECK_RUN("relocated-list", list, list.code == 0);
    NX_CHECK(list.out.find("id=2 kind=Video") != std::string::npos);
    const std::string relocated_path = g_root + "/relocated.jsonl";
    const auto extract =
        run_exporter({"jsonl", relocated, "--dataset", "1", "--output", relocated_path});
    NX_CHECK_RUN("relocated-jsonl", extract, extract.code == 0);
    NX_CHECK(parse_lines(read_file(relocated_path)).size() == 3U);
    const std::string relocated_video = g_root + "/relocated.h265";
    const auto video =
        run_exporter({"video", relocated, "--dataset", "2", "--output", relocated_video});
    NX_CHECK_RUN("relocated-video", video, video.code == 0);
    NX_CHECK(file_size(relocated_video) == 656U);
}

/// @brief An unclosed session is reported as incomplete, not as a clean close.
void test_unclosed_session() {
    const std::string copy = g_root + "/unclosed";
    NX_CHECK(copy_tree(g_session, copy));
    const std::string manifest_path = copy + "/session.json";
    auto manifest = parse_json(read_file(manifest_path));
    NX_CHECK(manifest.is_object());
    manifest["clean_close"] = false;
    if (manifest.contains("segments") && manifest["segments"].is_array()) {
        for (auto& segment : manifest["segments"]) {
            if (segment.is_object()) {
                segment["closed"] = false;
            }
        }
    }
    NX_CHECK(write_json(manifest_path, manifest));

    const std::string rec = copy + "/sess_000000.nxrec";
    const auto size = file_size(rec);
    NX_CHECK(size > sizeof(nexigen::recorder::SegmentFooter));
    std::error_code error;
    if (size > sizeof(nexigen::recorder::SegmentFooter)) {
        std::filesystem::resize_file(rec, size - sizeof(nexigen::recorder::SegmentFooter), error);
        NX_CHECK(!error);
    }

    const auto list = run_exporter({"list", copy, "--json"});
    NX_CHECK_RUN("unclosed-list", list, list.code == 0);
    const auto entries = parse_json(list.out);
    NX_CHECK(entries.is_array());
    if (entries.is_array()) {
        for (const auto& entry : entries) {
            NX_CHECK(entry.value("closed", true) == false);
        }
    }
    const auto inspect = run_exporter({"inspect", copy, "--json"});
    NX_CHECK_RUN("unclosed-inspect", inspect, inspect.code == 0);
    const auto parsed = parse_json(inspect.out);
    NX_CHECK(parsed.value("clean_close", true) == false);
    const auto verify = run_exporter({"verify", copy});
    NX_CHECK(verify.out.find("clean=false") != std::string::npos);
}

/// @brief Inspection commands work from metadata alone, without `.nxvid`.
void test_inspection_ignores_video_bytes() {
    const std::string copy = g_root + "/video-removed";
    NX_CHECK(copy_tree(g_session, copy));
    std::error_code error;
    NX_CHECK(std::filesystem::remove(copy + "/sess_000000.nxvid", error));
    NX_CHECK(!error);

    const auto no_video_list = run_exporter({"list", copy});
    NX_CHECK_RUN("no-video-list", no_video_list, no_video_list.code == 0);
    const auto no_video_inspect = run_exporter({"inspect", copy});
    NX_CHECK_RUN("no-video-inspect", no_video_inspect, no_video_inspect.code == 0);
    const auto no_video_info = run_exporter({"info", copy, "--dataset", "2"});
    NX_CHECK_RUN("no-video-info", no_video_info, no_video_info.code == 0);
    const auto verify = run_exporter({"verify", copy});
    NX_CHECK(verify.code == 1);
}

/// @brief Gap markers appear as explicit JSONL objects at their positions.
void test_gap_jsonl() {
    const std::string root = g_root + "/gap-session";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 4096;
    settings.index_interval_ns = 1;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = 0;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Gap";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto channel = writer->declare_channel(
        "test/gap", 1, nexigen::recorder::DatasetKind::Telemetry, "nexigen.pod");
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    const std::vector<std::byte> payload(8);
    NX_CHECK(
        writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload, 1U, 0U, 1U)
            .has_value());
    nexigen::recorder::GapMarkerPayload gap{};
    gap.first_lost_sequence = 1U;
    gap.lost_count = 2U;
    gap.reason = static_cast<std::uint16_t>(nexigen::recorder::GapReason::StreamDiscontinuity);
    const auto gap_bytes =
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(&gap), sizeof(gap));
    NX_CHECK(
        writer->append(nexigen::recorder::RecordType::GapMarker, *channel, gap_bytes, 2U, 1U, 1U)
            .has_value());
    NX_CHECK(
        writer->append(nexigen::recorder::RecordType::ComPayload, *channel, payload, 3U, 3U, 1U)
            .has_value());
    NX_CHECK(writer->close().has_value());

    const std::string output = g_root + "/gap.jsonl";
    const auto exported =
        run_exporter({"jsonl", root + "/sess", "--dataset", "1", "--output", output});
    NX_CHECK_RUN("gap-jsonl", exported, exported.code == 0);
    const auto lines = parse_lines(read_file(output));
    NX_CHECK(lines.size() == 3U);
    std::size_t gaps = 0;
    for (const auto& line : lines) {
        if (line.value("type", 0U) ==
            static_cast<std::uint16_t>(nexigen::recorder::RecordType::GapMarker)) {
            ++gaps;
            NX_CHECK(line["payload"]["first_lost_sequence"] == 1U);
            NX_CHECK(line["payload"]["lost_count"] == 2U);
        }
    }
    NX_CHECK(gaps == 1U);
}

/// @brief Uncompressed and JPEG XS frames are passed through byte-for-byte.
void test_video_codecs() {
    const std::string root = g_root + "/codec-session";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    nexigen::recorder::StorageSettings settings{};
    settings.session_root = root;
    settings.session_id = "sess";
    settings.video_frame_buffer_bytes = 8192;
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 4096;
    settings.index_interval_ns = 1;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = 0;
    auto writer = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(writer.has_value());
    if (!writer.has_value()) {
        return;
    }
    nexigen::recorder::SchemaDeclare schema{};
    schema.id = 1;
    schema.name = "test.Frames";
    schema.encoding = "jsonschema";
    schema.data = json_bytes(R"({"fields":[]})");
    NX_CHECK(writer->declare_schema(schema).has_value());
    const auto raw = writer->declare_channel("test/raw", 1, nexigen::recorder::DatasetKind::Video,
                                             "nexigen.pod");
    const auto jxs = writer->declare_channel("test/jxs", 1, nexigen::recorder::DatasetKind::Video,
                                             "nexigen.pod");
    NX_CHECK(raw.has_value() && jxs.has_value());
    if (!raw.has_value() || !jxs.has_value()) {
        return;
    }
    struct Stream {
        std::uint32_t channel;
        std::uint16_t codec;
        std::uint8_t seed;
        std::string name;
    };
    const std::array<Stream, 2> streams{{
        {*raw, static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::RawRgb24), 0x11U, "raw"},
        {*jxs, static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::JpegXs), 0x22U, "jxs"},
    }};
    for (const auto& stream : streams) {
        nexigen::recorder::VideoStreamConfig config{};
        config.codec = stream.codec;
        config.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::Raw);
        config.width = 32;
        config.height = 32;
        config.config_epoch = 1;
        const auto frame = make_frame(128U, stream.seed);
        for (std::uint64_t i = 0; i < 2; ++i) {
            nexigen::recorder::VideoFrameRef reference{};
            reference.capture_mono_ns = i * 20'000'000U;
            reference.source_sequence = i;
            reference.config_epoch = 1;
            NX_CHECK(writer->append_video(stream.channel, config, reference, frame).has_value());
        }
    }
    NX_CHECK(writer->close().has_value());
    for (const auto& stream : streams) {
        const auto frame = make_frame(128U, stream.seed);
        const std::string expected(frame.begin(), frame.end());
        const std::string output = g_root + "/codec-" + stream.name + ".bin";
        const auto result = run_exporter({"video", root + "/sess", "--dataset",
                                          std::to_string(stream.channel), "--output", output});
        NX_CHECK_RUN("codec", result, result.code == 0);
        NX_CHECK(read_file(output) == expected + expected);
    }
}

/// @brief Missing codec metadata and missing mandatory extradata are named.
void test_missing_video_metadata() {
    // (a) A frame reference with no VideoStreamConfig declaration.
    const std::string no_config_root = g_root + "/no-config";
    std::error_code error;
    std::filesystem::remove_all(no_config_root, error);
    std::filesystem::create_directories(no_config_root, error);
    nexigen::recorder::StorageSettings settings{};
    settings.session_root = no_config_root;
    settings.session_id = "sess";
    settings.write_buffer_bytes = 4096;
    settings.index_buffer_bytes = 4096;
    settings.index_stride_bytes = 4096;
    settings.index_interval_ns = 1;
    settings.max_record_bytes = 4096;
    settings.min_free_bytes = 0;
    auto no_config = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(no_config.has_value());
    if (no_config.has_value()) {
        nexigen::recorder::SchemaDeclare schema{};
        schema.id = 1;
        schema.name = "test.NoConfig";
        schema.encoding = "jsonschema";
        schema.data = json_bytes(R"({"fields":[]})");
        NX_CHECK(no_config->declare_schema(schema).has_value());
        const auto channel = no_config->declare_channel(
            "test/camera", 1, nexigen::recorder::DatasetKind::Video, "nexigen.pod");
        NX_CHECK(channel.has_value());
        if (channel.has_value()) {
            nexigen::recorder::VideoFrameRef reference{};
            reference.vid_offset = 0;
            reference.vid_size = 16;
            reference.config_epoch = 1;
            const auto bytes = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(&reference), sizeof(reference));
            NX_CHECK(no_config
                         ->append(nexigen::recorder::RecordType::VideoFrameRef, *channel, bytes, 0U,
                                  0U, 1U)
                         .has_value());
        }
        NX_CHECK(no_config->close().has_value());
    }
    const auto missing_config = run_exporter({"video", no_config_root + "/sess", "--dataset", "1",
                                              "--output", g_root + "/no-config.bin"});
    NX_CHECK(missing_config.code != 0);
    NX_CHECK(missing_config.err.find("missing codec configuration") != std::string::npos);

    // (b) Length-prefixed H.265 without extradata.
    const std::string no_extradata_root = g_root + "/no-extradata";
    std::filesystem::remove_all(no_extradata_root, error);
    std::filesystem::create_directories(no_extradata_root, error);
    settings.session_root = no_extradata_root;
    settings.video_frame_buffer_bytes = 8192;
    auto no_extradata = nexigen::recorder::SessionWriter::create(settings);
    NX_CHECK(no_extradata.has_value());
    if (no_extradata.has_value()) {
        nexigen::recorder::SchemaDeclare schema{};
        schema.id = 1;
        schema.name = "test.NoExtradata";
        schema.encoding = "jsonschema";
        schema.data = json_bytes(R"({"fields":[]})");
        NX_CHECK(no_extradata->declare_schema(schema).has_value());
        const auto channel = no_extradata->declare_channel(
            "test/camera", 1, nexigen::recorder::DatasetKind::Video, "nexigen.pod");
        NX_CHECK(channel.has_value());
        if (channel.has_value()) {
            nexigen::recorder::VideoStreamConfig config{};
            config.codec = static_cast<std::uint16_t>(nexigen::recorder::VideoCodec::H265);
            config.bitstream = static_cast<std::uint16_t>(nexigen::recorder::BitstreamFormat::Avcc);
            config.config_epoch = 1;
            const auto frame = make_frame(64U, 0x33U);
            nexigen::recorder::VideoFrameRef reference{};
            reference.capture_mono_ns = 1;
            reference.source_sequence = 0;
            reference.config_epoch = 1;
            NX_CHECK(no_extradata->append_video(*channel, config, reference, frame).has_value());
        }
        NX_CHECK(no_extradata->close().has_value());
    }
    const auto missing_extradata = run_exporter({"video", no_extradata_root + "/sess", "--dataset",
                                                 "1", "--output", g_root + "/no-extradata.bin"});
    NX_CHECK(missing_extradata.code != 0);
    NX_CHECK(missing_extradata.err.find("missing codec extradata") != std::string::npos);
}

void test_mcap() {
    const std::string path = g_root + "/export.mcap";
    const auto result = run_exporter(
        {"extract", g_session, "--channels", "1,2,3,4", "--format", "mcap", "--output", path});
    if (!g_mcap_enabled) {
        NX_CHECK_RUN("mcap-disabled", result, result.code == 1);
        NX_CHECK(result.err.find("NEXIGEN_ENABLE_MCAP=OFF") != std::string::npos);
        NX_CHECK(!std::filesystem::exists(path));
        return;
    }
    NX_CHECK_RUN("mcap", result, result.code == 0);
    const std::string bytes = read_file(path);
    NX_CHECK(bytes.size() > 64U);
    NX_CHECK(!bytes.empty() && static_cast<unsigned char>(bytes[0]) == 0x89U);
    NX_CHECK(bytes.size() >= 8U && bytes.compare(1U, 7U, "MCAP0\r\n") == 0);
    NX_CHECK(bytes.find("test/log") != std::string::npos);
    NX_CHECK(bytes.find("test/camera") != std::string::npos);
    NX_CHECK(bytes.find("test/opaque") != std::string::npos);
    NX_CHECK(bytes.find("nexigen.log.LogFrame") != std::string::npos);
    NX_CHECK(bytes.find("protobuf") != std::string::npos);

    // TimeSync calibration converts commit time to UTC epoch nanoseconds:
    // each expected u64 timestamp must appear in the MCAP payload.
    auto reader = nexigen::recorder::SessionReader::open(g_session);
    NX_CHECK(reader.has_value());
    if (reader.has_value()) {
        std::vector<std::uint64_t> commits;
        const auto collector = [](void* context, const nexigen::recorder::RecordHeader& header,
                                  std::span<const std::byte>, std::uint64_t) {
            if (header.channel_id != 0U) {
                static_cast<std::vector<std::uint64_t>*>(context)->push_back(header.commit_mono_ns);
            }
            return true;
        };
        NX_CHECK(reader->for_each(0, false, 0, 0, false, collector, &commits).has_value());
        NX_CHECK(!commits.empty());
        std::size_t found = 0;
        for (const auto commit : commits) {
            if (mcap_contains_u64(bytes, commit + 1'700'000'000'000'000'000ULL)) {
                ++found;
            }
        }
        NX_CHECK(found == commits.size());
    }
}

void test_time_base_resolution() {
    const std::string valid_root = g_root + "/tb-valid";
    const std::string legacy_root = g_root + "/tb-legacy";
    NX_CHECK(write_time_base_fixture(valid_root, false));
    NX_CHECK(write_time_base_fixture(legacy_root, true));
    const std::string gap_root = g_root + "/tb-gap";
    NX_CHECK(write_time_base_fixture(gap_root, false, true));

    const auto verified = run_exporter({"verify", valid_root + "/sess"});
    NX_CHECK_RUN("time-base-verify", verified, verified.code == 0);
    NX_CHECK(verified.out.find("clean=true") != std::string::npos);

    const std::string path = g_root + "/time-base.jsonl";
    const auto run =
        run_exporter({"jsonl", valid_root + "/sess", "--dataset", "1", "--output", path});
    NX_CHECK_RUN("time-base-jsonl", run, run.code == 0);
    const auto lines = parse_lines(read_file(path));
    NX_CHECK(lines.size() == 1U);
    if (lines.size() == 1U) {
        NX_CHECK(lines[0]["time_base"] == "resolved");
        NX_CHECK(lines[0]["native_ns"] == 1'500U);
        NX_CHECK(lines[0]["native_domain"] == "device");
        NX_CHECK(lines[0]["domain_id"] == 7U);
        NX_CHECK(lines[0]["domain_epoch"] == 5U);
        NX_CHECK(lines[0]["canonical_ns"] == 2'500U);
        NX_CHECK(lines[0]["utc_ns"] == kFixtureRealtimeBase + 500U);
        NX_CHECK(lines[0].contains("tai_ns"));
        NX_CHECK(lines[0].contains("utc"));
        NX_CHECK(lines[0].contains("boot"));
    }

    const std::string gap_path = g_root + "/time-base-gap.jsonl";
    const auto gap_run =
        run_exporter({"jsonl", gap_root + "/sess", "--dataset", "1", "--output", gap_path});
    NX_CHECK_RUN("time-base-gap-jsonl", gap_run, gap_run.code == 0);
    const auto gap_lines = parse_lines(read_file(gap_path));
    NX_CHECK(gap_lines.size() == 1U);
    if (gap_lines.size() == 1U) {
        NX_CHECK(gap_lines[0]["time_base"] == "unresolved");
        NX_CHECK(!gap_lines[0].contains("utc_ns"));
        NX_CHECK(!gap_lines[0].contains("native_ns"));
    }

    const std::string legacy_path = g_root + "/time-base-legacy.jsonl";
    const auto legacy =
        run_exporter({"jsonl", legacy_root + "/sess", "--dataset", "1", "--output", legacy_path});
    NX_CHECK_RUN("time-base-legacy-jsonl", legacy, legacy.code == 0);
    const auto legacy_lines = parse_lines(read_file(legacy_path));
    NX_CHECK(legacy_lines.size() == 1U);
    if (legacy_lines.size() == 1U) {
        NX_CHECK(legacy_lines[0]["time_base"] == "legacy");
        NX_CHECK(!legacy_lines[0].contains("utc_ns"));
        NX_CHECK(!legacy_lines[0].contains("native_ns"));
        NX_CHECK(!legacy_lines[0].contains("boot"));
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <nexigen-export> [mcap-on|mcap-off]\n", argv[0]);
        return 2;
    }
    g_exporter = argv[1];
    g_mcap_enabled = argc > 2 && std::string_view(argv[2]) == "mcap-on";

    std::error_code error;
    // Respect TMPDIR: CI images and sandboxes rarely pre-create a fixed prefix.
    const auto temp_base = std::filesystem::temp_directory_path(error);
    const std::string base = error ? std::string{"/tmp"} : temp_base.string();
    std::string pattern = base + "/nexigen_export_cli_XXXXXX";
    std::vector<char> pattern_buffer(pattern.begin(), pattern.end());
    pattern_buffer.push_back('\0');
    char* created = ::mkdtemp(pattern_buffer.data());
    if (created == nullptr) {
        std::fprintf(stderr, "cannot create temporary session root\n");
        return 2;
    }
    g_root = created;
    g_session = g_root + "/sess";
    if (!write_fixture()) {
        std::fprintf(stderr, "cannot create the fixture session under %s\n", g_root.c_str());
        return 2;
    }

    test_help_and_usage();
    test_list_and_inspect();
    test_info();
    test_jsonl_and_ranges();
    test_perfetto();
    test_video();
    test_format_rejections();
    test_verify();
    test_relocated_session();
    test_unclosed_session();
    test_inspection_ignores_video_bytes();
    test_gap_jsonl();
    test_video_codecs();
    test_missing_video_metadata();
    test_time_base_resolution();
    test_mcap();

    std::printf("export CLI tests: %d checks, %d failed\n", g_checks, g_failures);
    if (std::getenv("NEXIGEN_EXPORT_CLI_KEEP") == nullptr) {
        std::filesystem::remove_all(g_root, error);
    }
    return g_failures == 0 ? 0 : 1;
}
