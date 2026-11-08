// Split from tracing_test.cpp: nexigen::tracing sinks.

#include "tracing_test_support.hpp"

namespace {

static_assert(tracing::concepts::TraceSink<tracing::ConsoleTraceSink>);
static_assert(tracing::concepts::TraceSink<tracing::JsonFileTraceSink>);
static_assert(tracing::concepts::TraceSink<tracing::ChromeTraceSink>);
static_assert(tracing::concepts::TraceSink<tracing::NullTraceSink>);
static_assert(tracing::concepts::TraceSink<tracing::CompositeTraceSink<>>);
static_assert(tracing::concepts::TraceSink<
              tracing::CompositeTraceSink<tracing::ConsoleTraceSink, tracing::NullTraceSink>>);

// Compile-failure proof for task 7.4 (spec composite "Members are reachable
// by index and type" — out-of-range/absent access fails compilation) and
// task 7.1 (live sinks refuse deserialization). Verified by running the
// commands, capturing the error, then deleting the file:
//   cat > /tmp/opencode/neg/sinks_neg.cpp <<'EOF'
//   #include <nlohmann/json.hpp>
//   #include <nexigen/tracing/composite_trace_sink.hpp>
//   #include <nexigen/tracing/console_trace_sink.hpp>
//   #include <nexigen/tracing/null_trace_sink.hpp>
//   int main() {
//       nexigen::tracing::CompositeTraceSink<
//           nexigen::tracing::ConsoleTraceSink,
//           nexigen::tracing::NullTraceSink> c{
//           nexigen::tracing::ConsoleTraceSink{1}, nexigen::tracing::NullTraceSink{}};
//       static_cast<void>(c.get<2U>());          // index beyond pack
//       static_cast<void>(c.get<int>());         // absent type
//       nlohmann::json j;
//       nexigen::tracing::ConsoleTraceSink s{1};
//       nlohmann::from_json(j, s);               // live sink: no from_json
//   }
//   EOF
//   g++ -std=c++23 -fno-exceptions -fno-rtti -D_GNU_SOURCE -DJSON_NOEXCEPTION=1
//       -Iinclude -c /tmp/opencode/neg/sinks_neg.cpp -o /dev/null
//       # expect: static assertion failed (x2) + no matching from_json

// --- sinks suite (tasks 7.1..7.4 / spec tracing/trace-sinks): console
// content through a pipe fixture, JSONL round-trips read back line by line,
// ISO-8601 shape, typed attribute serialization with escaping, rotation
// filenames per policy, the rate-limit drop summary, the Chrome object
// parse with complete-event coverage, composite fan-out/no-starvation, and
// every error path — all against crafted records (full control of the UTC
// windows driving rotation).
namespace {

struct FailingOnSpanSink {
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    core::Status flush() noexcept { return {}; }
    void stop() noexcept {}
    std::string to_string() const { return "FailingOnSpanSink"; }
};
static_assert(tracing::concepts::TraceSink<FailingOnSpanSink>);

/// A copyable counting sink (composite no-starvation proof: the member
/// inside the composite counts its own deliveries).
struct CountingSink {
    int deliveries{0};
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord&) noexcept {
        ++deliveries;
        return {};
    }
    core::Status flush() noexcept { return {}; }
    void stop() noexcept {}
    std::string to_string() const { return "CountingSink"; }
};
static_assert(tracing::concepts::TraceSink<CountingSink>);

} // namespace

/// ISO-8601 shape predicate: YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ (30 chars).
[[nodiscard]] bool iso_shape(std::string_view s) {
    return s.size() == 30U && s[4] == '-' && s[7] == '-' && s[10] == 'T' && s[13] == ':' &&
           s[16] == ':' && s[19] == '.' && s[29] == 'Z';
}

void test_sinks_console() {
    // ---- ConsoleTraceSink (task 7.1) --------------------------------------
    {
        // Content through a pipe fixture: one line per span carrying ids,
        // parent, kind, status, name, duration, and every attribute.
        int fds[2] = {-1, -1};
        NX_CHECK(::pipe(fds) == 0);
        tracing::ConsoleTraceSink sink{fds[1]};
        NX_CHECK(sink.start().has_value());
        NX_CHECK(sink.on_span(crafted_record(1'000'000'000ULL, "query")).has_value());
        NX_CHECK(sink.flush().has_value()); // no-op success
        // Read the line back from the pipe.
        char pipe_buffer[2048];
        std::size_t total = 0;
        while (total == 0U) {
            const ssize_t got = ::read(fds[0], pipe_buffer + total, sizeof(pipe_buffer) - total);
            if (got <= 0) {
                break;
            }
            total += static_cast<std::size_t>(got);
        }
        const std::string_view line(pipe_buffer, total);
        NX_CHECK(line.find("[trace 0123456789abcdef]") != std::string_view::npos);
        NX_CHECK(line.find("span=fedcba9876543210") != std::string_view::npos);
        NX_CHECK(line.find("parent=0000000000000001") != std::string_view::npos);
        NX_CHECK(line.find("kind=Server") != std::string_view::npos);
        NX_CHECK(line.find("status=Ok") != std::string_view::npos);
        NX_CHECK(line.find("name=\"query\"") != std::string_view::npos);
        NX_CHECK(line.find("dur=1500ns") != std::string_view::npos);
        NX_CHECK(line.find("tid=4242") != std::string_view::npos);
        NX_CHECK(line.find("retries=3") != std::string_view::npos);
        NX_CHECK(line.find("ratio=0.15625") != std::string_view::npos);
        NX_CHECK(line.find("ok=true") != std::string_view::npos);
        NX_CHECK(line.find("tag=\"hil\"") != std::string_view::npos);
        NX_CHECK(!line.empty() && line.back() == '\n');
        static_cast<void>(::close(fds[0]));
        static_cast<void>(::close(fds[1]));

        // Zero allocation per delivered record. /dev/null: a real write
        // destination that can never block (a pipe would fill after ~460
        // lines with no reader).
        {
            const std::size_t arena_before = arena_used();
            const std::size_t heap_before = heap_in_use();
            const int devnull = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
            NX_CHECK(devnull >= 0);
            tracing::ConsoleTraceSink loop_sink{devnull};
            const tracing::SpanRecord record = crafted_record(2'000'000'000ULL, "alloc-free");
            for (int i = 0; i < 500; ++i) {
                static_cast<void>(loop_sink.on_span(record));
            }
            NX_CHECK(arena_used() == arena_before);
            NX_CHECK(heap_in_use() == heap_before);
            static_cast<void>(::close(devnull));
        }

        // Closed descriptor: SystemError without abort; the sink stays
        // usable for error reporting (a follow-up call returns again).
        {
            const int stale = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
            NX_CHECK(stale >= 0);
            static_cast<void>(::close(stale));
            tracing::ConsoleTraceSink dead_sink{stale};
            const core::Status failed =
                dead_sink.on_span(crafted_record(3'000'000'000ULL, "doomed"));
            NX_CHECK(!failed.has_value());
            NX_CHECK(failed.error() == core::ErrorCode::SystemError);
            NX_CHECK(!dead_sink.on_span(crafted_record(3'000'000'000ULL, "again"))
                          .has_value()); // still usable, still SystemError
            dead_sink.stop();
        }

        // Diagnostics: to_string/formatter/to_json (no from_json — pinned
        // by the documented scratch-TU compile check above).
        {
            tracing::ConsoleTraceSink diag{2};
            NX_CHECK(diag.to_string() == "ConsoleTraceSink(fd=2)");
            NX_CHECK(tracing::to_string(diag) == diag.to_string());
            NX_CHECK(std::format("{}", diag) == diag.to_string());
            const nlohmann::json j = diag;
            NX_CHECK(j.at("type").get<std::string>() == "ConsoleTraceSink");
            NX_CHECK(j.at("fd").get<int>() == 2);
        }
    }
}

// Forward declarations: these helpers are defined after test_sinks() so the
// file order of the suite's NX_CHECK statements matches the original layout.
template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_basic(const chrono::CivilTime& now_civil, const HourMidUtc& hour_mid_utc,
                            const MonoForUtc& mono_for_utc);
template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_overflow(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc);
template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_escape(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc);
template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_rotation(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc);
template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_rate_limit(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc);
template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_open_failure(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc);
void test_sinks_config_diagnostics();
void test_sinks_chrome();
template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_composite(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc);

void test_sinks() {
    static_cast<void>(::mkdir(sink_tmp_dir, 0755)); // exists is fine

    test_sinks_console();

    // Shared UTC facts for the crafted-window tests below.
    std::int64_t utc_offset = 0;
    NX_CHECK(chrono::sample_utc_offset(utc_offset).has_value());
    const std::int64_t now_utc =
        chrono::utc_from_mono_ns(chrono::Timestamp::now()->nanoseconds(), utc_offset);
    const chrono::CivilTime now_civil = chrono::civil_from_utc_ns(now_utc);
    // A safe mid-window instant: 30 minutes past the current hour.
    const auto hour_mid_utc = [&](std::int64_t day_offset_hours) -> std::int64_t {
        // Base: the top of the current hour, then +30 min, plus day_offset.
        const std::int64_t ns_per_hour = 3'600'000'000'000LL;
        const std::int64_t hour_top =
            now_utc - (now_utc % ns_per_hour) - (now_utc < 0 ? ns_per_hour : 0);
        return hour_top + 1'800'000'000'000LL + day_offset_hours * ns_per_hour;
    };
    // Convert a target UTC instant into a monotonic-domain record start.
    const auto mono_for_utc = [&](std::int64_t utc_ns) -> std::uint64_t {
        return static_cast<std::uint64_t>(utc_ns - utc_offset);
    };

    test_sinks_jsonl_basic(now_civil, hour_mid_utc, mono_for_utc);

    test_sinks_jsonl_overflow(hour_mid_utc, mono_for_utc);

    test_sinks_jsonl_escape(hour_mid_utc, mono_for_utc);

    test_sinks_jsonl_rotation(hour_mid_utc, mono_for_utc);

    test_sinks_jsonl_rate_limit(hour_mid_utc, mono_for_utc);

    test_sinks_jsonl_open_failure(hour_mid_utc, mono_for_utc);

    test_sinks_config_diagnostics();

    test_sinks_chrome();

    test_sinks_composite(hour_mid_utc, mono_for_utc);
}

template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_basic(const chrono::CivilTime& now_civil, const HourMidUtc& hour_mid_utc,
                            const MonoForUtc& mono_for_utc) {
    // ---- JsonFileTraceSink: one parseable object per line (task 7.2) ----
    {
        const std::string path = std::string(sink_tmp_dir) + "/jsonl-basic.jsonl";
        static_cast<void>(::unlink(path.c_str()));
        tracing::JsonFileTraceSinkConfig config{};
        config.path = path;
        tracing::JsonFileTraceSink sink{config};
        NX_CHECK(sink.start().has_value());
        const tracing::SpanRecord record =
            crafted_record(mono_for_utc(hour_mid_utc(0)), "basic-span");
        NX_CHECK(sink.on_span(record).has_value());
        NX_CHECK(sink.flush().has_value()); // fsync surfaced
        sink.stop();

        const std::string content = read_file(path.c_str());
        NX_CHECK(!content.empty());
        // One parseable JSON object per line; each field round-trips.
        std::size_t line_start = 0;
        std::size_t lines = 0;
        nlohmann::json parsed{};
        while (line_start < content.size()) {
            const std::size_t line_end = content.find('\n', line_start);
            const std::size_t line_len = (line_end == std::string::npos)
                                             ? content.size() - line_start
                                             : line_end - line_start;
            NX_CHECK(line_len > 0U);
            parsed = nlohmann::json::parse(content.substr(line_start, line_len));
            ++lines;
            line_start = (line_end == std::string::npos) ? content.size() : line_end + 1U;
        }
        NX_CHECK(lines == 1);
        NX_CHECK(parsed.at("trace_id").get<std::string>() == "0123456789abcdef");
        NX_CHECK(parsed.at("span_id").get<std::string>() == "fedcba9876543210");
        NX_CHECK(parsed.at("parent_span_id").get<std::string>() == "0000000000000001");
        NX_CHECK(parsed.at("kind").get<std::string>() == "Server");
        NX_CHECK(parsed.at("status").get<std::string>() == "Ok");
        NX_CHECK(parsed.at("thread_id").get<std::uint32_t>() == 4242);
        NX_CHECK(parsed.at("duration_ns").get<std::uint64_t>() == 1'500);
        NX_CHECK(parsed.at("name").get<std::string>() == "basic-span");
        NX_CHECK(parsed.at("file").get<std::string>() == "engine.cpp");
        NX_CHECK(parsed.at("line").get<std::uint32_t>() == 77);
        // ISO-8601 shape: YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ (30 chars).
        const std::string start_iso = parsed.at("start").get<std::string>();
        const std::string end_iso = parsed.at("end").get<std::string>();
        NX_CHECK(iso_shape(start_iso));
        NX_CHECK(iso_shape(end_iso));
        // The ISO text embeds the crafted civil hour.
        const std::string hour_text =
            std::to_string(now_civil.year) + "-";     // build YYYY-MM-DDTHH prefix
        NX_CHECK(start_iso.rfind(hour_text, 0) == 0); // begins with the year
        // Typed attributes round-trip.
        const nlohmann::json& attrs = parsed.at("attributes");
        NX_CHECK(attrs.is_array() && attrs.size() == 4);
        NX_CHECK(attrs[0].at("key").get<std::string>() == "retries" &&
                 attrs[0].at("type").get<std::string>() == "Int64" &&
                 attrs[0].at("value").get<std::int64_t>() == 3);
        NX_CHECK(attrs[1].at("type").get<std::string>() == "Double" &&
                 attrs[1].at("value").get<double>() == 0.15625);
        NX_CHECK(attrs[2].at("type").get<std::string>() == "Bool" &&
                 attrs[2].at("value").get<bool>());
        NX_CHECK(attrs[3].at("type").get<std::string>() == "String" &&
                 attrs[3].at("value").get<std::string>() == "hil");
    }
}

template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_overflow(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc) {
    // ---- Scratch overflow: discards the line and COUNTS it (P2-18) ----
    {
        const std::string path = std::string(sink_tmp_dir) + "/jsonl-overflow.jsonl";
        static_cast<void>(::unlink(path.c_str()));
        tracing::JsonFileTraceSinkConfig config{};
        config.path = path;
        tracing::JsonFileTraceSink sink{config};
        NX_CHECK(sink.start().has_value());

        tracing::SpanRecord big = crafted_record(mono_for_utc(hour_mid_utc(0)), "overflow-span");
        // A 9 KiB string-view attribute cannot fit the 8 KiB scratch no
        // matter the escaping (zero-copy string views are representable):
        // the writer's ok() turns false.
        static char huge_payload[9216];
        for (char& byte : huge_payload) {
            byte = 'x'; // un-escaped bulk: pure length, no escaping inflation
        }
        static_cast<void>(big.attributes[0].store(
            "payload", std::string_view(huge_payload, sizeof huge_payload)));
        big.attribute_count = 1;
        const core::Status overflow = sink.on_span(big);
        NX_CHECK(!overflow.has_value() && overflow.error() == core::ErrorCode::BufferFull);
        NX_CHECK(sink.to_string().find("discarded_lines=1") != std::string::npos);
        NX_CHECK(sink.flush().has_value()); // no I/O failure hides the discard

        // Nothing partial was written: the file stays empty (or absent).
        const std::string content = read_file(path.c_str());
        NX_CHECK(content.empty());
    }
}

template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_escape(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc) {
    // ---- JsonFileTraceSink: escaping of quotes/backslashes/control chars.
    {
        const std::string path = std::string(sink_tmp_dir) + "/jsonl-escape.jsonl";
        static_cast<void>(::unlink(path.c_str()));
        tracing::JsonFileTraceSinkConfig config{};
        config.path = path;
        tracing::JsonFileTraceSink sink{config};
        NX_CHECK(sink.start().has_value());
        tracing::SpanRecord record = crafted_record(mono_for_utc(hour_mid_utc(0)), "esc\"ap\\e\nx");
        static_cast<void>(record.attributes[3].store("ctl", std::string_view("tab\tend\"q\\")));
        NX_CHECK(sink.on_span(record).has_value());
        sink.stop();
        const std::string content = read_file(path.c_str());
        const nlohmann::json parsed = nlohmann::json::parse(content);
        // The escaped name and attribute text round-trip exactly.
        NX_CHECK(parsed.at("name").get<std::string>() == "esc\"ap\\e\nx");
        NX_CHECK(parsed.at("attributes")[3].at("value").get<std::string>() == "tab\tend\"q\\");
    }
}

template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_rotation(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc) {
    // ---- JsonFileTraceSink: rotation filenames per policy (task 7.2) ----
    {
        // Hourly: two crafted spans in different UTC hours → two files, the
        // name embedding each span's hour; contents land in their window's
        // file.
        const std::string prefix = std::string(sink_tmp_dir) + "/jsonl-rot";
        for (const auto policy :
             {tracing::RotationPolicy::Hourly, tracing::RotationPolicy::Daily}) {
            tracing::JsonFileTraceSinkConfig config{};
            config.path = prefix;
            config.rotation = policy;
            tracing::JsonFileTraceSink sink{config};
            NX_CHECK(sink.start().has_value());
            // Span A: 30 min into the current hour/day.
            NX_CHECK(sink.on_span(crafted_record(mono_for_utc(hour_mid_utc(0)), "window-a"))
                         .has_value());
            // Span B: one period later (next hour / next day).
            const std::int64_t period_ns = policy == tracing::RotationPolicy::Hourly
                                               ? 3'600'000'000'000LL
                                               : 86'400'000'000'000LL;
            NX_CHECK(
                sink.on_span(crafted_record(mono_for_utc(hour_mid_utc(0) + period_ns), "window-b"))
                    .has_value());
            sink.stop();

            // Expected labels from the CIVIL time of each crafted window
            // (compact form: YYYYMMDDTHH hourly, YYYYMMDD daily).
            const auto label_of = [&](std::int64_t utc) {
                const chrono::CivilTime civil = chrono::civil_from_utc_ns(utc);
                auto pad = [](std::int64_t v, int width) {
                    std::string s = std::to_string(v < 0 ? -v : v);
                    while (static_cast<int>(s.size()) < width) {
                        s.insert(0, "0");
                    }
                    return s;
                };
                std::string label = pad(civil.year, 4) + pad(civil.month, 2) + pad(civil.day, 2);
                if (policy == tracing::RotationPolicy::Hourly) {
                    label += "T" + pad(civil.hour, 2);
                }
                return label;
            };
            const std::string file_a = prefix + "." + label_of(hour_mid_utc(0));
            const std::string file_b = prefix + "." + label_of(hour_mid_utc(0) + period_ns);
            NX_CHECK(file_a != file_b);
            const std::string content_a = read_file(file_a.c_str());
            const std::string content_b = read_file(file_b.c_str());
            NX_CHECK(content_a.find("window-a") != std::string::npos);
            NX_CHECK(content_a.find("window-b") == std::string::npos);
            NX_CHECK(content_b.find("window-b") != std::string::npos);
            static_cast<void>(::unlink(file_a.c_str()));
            static_cast<void>(::unlink(file_b.c_str()));
        }

        // None: all spans land in the single configured file for the
        // sink's lifetime.
        const std::string single = std::string(sink_tmp_dir) + "/jsonl-single.jsonl";
        static_cast<void>(::unlink(single.c_str()));
        tracing::JsonFileTraceSinkConfig none_config{};
        none_config.path = single;
        none_config.rotation = tracing::RotationPolicy::None;
        tracing::JsonFileTraceSink none_sink{none_config};
        NX_CHECK(none_sink.start().has_value());
        NX_CHECK(
            none_sink.on_span(crafted_record(mono_for_utc(hour_mid_utc(0)), "s-one")).has_value());
        NX_CHECK(
            none_sink.on_span(crafted_record(mono_for_utc(hour_mid_utc(48)), "s-two")).has_value());
        none_sink.stop();
        const std::string single_content = read_file(single.c_str());
        NX_CHECK(single_content.find("s-one") != std::string::npos);
        NX_CHECK(single_content.find("s-two") != std::string::npos);
    }
}

template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_rate_limit(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc) {
    // ---- JsonFileTraceSink: the rate-limit drop summary (task 7.2) -----
    {
        const std::string path = std::string(sink_tmp_dir) + "/jsonl-rate.jsonl";
        static_cast<void>(::unlink(path.c_str()));
        tracing::JsonFileTraceSinkConfig config{};
        config.path = path;
        config.rotation = tracing::RotationPolicy::None;
        config.max_spans_per_window = 2;
        config.window_ns = 60'000'000'000ULL; // 1 minute
        tracing::JsonFileTraceSink sink{config};
        NX_CHECK(sink.start().has_value());
        const std::uint64_t w0 = mono_for_utc(hour_mid_utc(0));
        // Two in-budget spans.
        NX_CHECK(sink.on_span(crafted_record(w0, "budget-1")).has_value());
        NX_CHECK(sink.on_span(crafted_record(w0 + 1'000, "budget-2")).has_value());
        // Two beyond the budget: NOT written, exactly counted.
        NX_CHECK(sink.on_span(crafted_record(w0 + 2'000, "over-1")).has_value());
        NX_CHECK(sink.on_span(crafted_record(w0 + 3'000, "over-2")).has_value());
        // A record past the window boundary: the summary line lands BEFORE
        // this record's line.
        NX_CHECK(sink.on_span(crafted_record(w0 + 61'000'000'000ULL, "next-window")).has_value());
        sink.stop();

        const std::string content = read_file(path.c_str());
        // Parse line by line: 2 records + 1 summary + 1 record, in order.
        std::vector<std::string> lines;
        std::size_t cursor = 0;
        while (cursor < content.size()) {
            const std::size_t end = content.find('\n', cursor);
            lines.push_back(content.substr(cursor, end - cursor));
            cursor = end + 1U;
        }
        NX_CHECK(lines.size() == 4U);
        const nlohmann::json l0 = nlohmann::json::parse(lines[0]);
        const nlohmann::json l1 = nlohmann::json::parse(lines[1]);
        const nlohmann::json l2 = nlohmann::json::parse(lines[2]);
        const nlohmann::json l3 = nlohmann::json::parse(lines[3]);
        NX_CHECK(l0.at("name").get<std::string>() == "budget-1");
        NX_CHECK(l1.at("name").get<std::string>() == "budget-2");
        NX_CHECK(l2.at("drop_summary").at("dropped").get<std::uint64_t>() == 2);
        NX_CHECK(l3.at("name").get<std::string>() == "next-window");
        // The over-budget spans were never written.
        NX_CHECK(content.find("over-1") == std::string::npos);
        NX_CHECK(content.find("over-2") == std::string::npos);

        // Unthrottled sinks write everything and no summaries appear.
        const std::string all_path = std::string(sink_tmp_dir) + "/jsonl-all.jsonl";
        static_cast<void>(::unlink(all_path.c_str()));
        tracing::JsonFileTraceSinkConfig all_config{};
        all_config.path = all_path;
        tracing::JsonFileTraceSink all_sink{all_config};
        NX_CHECK(all_sink.start().has_value());
        for (int i = 0; i < 10; ++i) {
            NX_CHECK(
                all_sink.on_span(crafted_record(w0 + static_cast<std::uint64_t>(i), "unthrottled"))
                    .has_value());
        }
        all_sink.stop();
        const std::string all_content = read_file(all_path.c_str());
        NX_CHECK(all_content.find("drop_summary") == std::string::npos);
        NX_CHECK(std::count(all_content.begin(), all_content.end(), '\n') == 10);
    }
}

template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_jsonl_open_failure(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc) {
    // ---- JsonFileTraceSink: open failure surfaces (task 7.2) -----------
    {
        tracing::JsonFileTraceSinkConfig config{};
        config.path = "/tmp/opencode/nexigen-tracing-tests/missing-dir/x.jsonl";
        tracing::JsonFileTraceSink sink{config};
        const core::Status started = sink.start();
        NX_CHECK(!started.has_value());
        NX_CHECK(started.error() == core::ErrorCode::SystemError);
        const core::Status delivered =
            sink.on_span(crafted_record(mono_for_utc(hour_mid_utc(0)), "void"));
        NX_CHECK(!delivered.has_value());
        NX_CHECK(delivered.error() == core::ErrorCode::SystemError);
        NX_CHECK(!sink.flush().has_value());
        sink.stop();
    }
}

void test_sinks_config_diagnostics() {
    // ---- JsonFileTraceSinkConfig + RotationPolicy trios (task 7.2) ----
    {
        // RotationPolicy: names round-trip, out-of-range renders Unknown,
        // hostile JSON leaves values, format honors width/alignment.
        NX_CHECK(tracing::to_string(tracing::RotationPolicy::None) == "None");
        NX_CHECK(tracing::to_string(tracing::RotationPolicy::Hourly) == "Hourly");
        NX_CHECK(tracing::to_string(tracing::RotationPolicy::Daily) == "Daily");
        NX_CHECK(tracing::to_string(static_cast<tracing::RotationPolicy>(9)) == "Unknown");
        tracing::RotationPolicy parsed = tracing::RotationPolicy::None;
        NX_CHECK(tracing::from_string("Daily", parsed) && parsed == tracing::RotationPolicy::Daily);
        NX_CHECK(!tracing::from_string("daily", parsed));
        NX_CHECK(parsed == tracing::RotationPolicy::Daily); // unchanged
        const nlohmann::json j_policy = tracing::RotationPolicy::Hourly;
        NX_CHECK(j_policy.get<std::string>() == "Hourly");
        tracing::RotationPolicy back = tracing::RotationPolicy::None;
        nlohmann::from_json(j_policy, back);
        NX_CHECK(back == tracing::RotationPolicy::Hourly);
        for (const nlohmann::json& bad : {nlohmann::json(1), nlohmann::json{1, 2},
                                          nlohmann::json(nullptr), nlohmann::json("nope")}) {
            tracing::RotationPolicy hostile = tracing::RotationPolicy::Daily;
            nlohmann::from_json(bad, hostile);
            NX_CHECK(hostile == tracing::RotationPolicy::Daily);
        }
        NX_CHECK(std::format("{}", tracing::RotationPolicy::Daily) == "Daily");
        NX_CHECK(std::format("{:>8}", tracing::RotationPolicy::None) == "    None");
        NX_CHECK(std::format("{:*<8}", tracing::RotationPolicy::Hourly) == "Hourly**");

        // The config round-trips every field under JSON_NOEXCEPTION=1.
        const tracing::JsonFileTraceSinkConfig config{
            "/tmp/x.jsonl", "controller", tracing::RotationPolicy::Hourly, 100, 500'000'000};
        NX_CHECK(config.to_string() == tracing::to_string(config));
        NX_CHECK(std::format("{}", config) == config.to_string());
        const nlohmann::json j = config;
        tracing::JsonFileTraceSinkConfig parsed_config{};
        nlohmann::from_json(j, parsed_config);
        NX_CHECK(parsed_config.path == config.path);
        NX_CHECK(parsed_config.app_name == config.app_name);
        NX_CHECK(parsed_config.rotation == config.rotation);
        NX_CHECK(parsed_config.max_spans_per_window == 100);
        NX_CHECK(parsed_config.window_ns == 500'000'000);
        // Hostile JSON leaves the values untouched.
        const tracing::JsonFileTraceSinkConfig before = parsed_config;
        for (const nlohmann::json& bad :
             {nlohmann::json(9), nlohmann::json{1}, nlohmann::json(nullptr),
              nlohmann::json{{"path", 1}, {"rotation", false}}}) {
            nlohmann::from_json(bad, parsed_config);
            NX_CHECK(parsed_config.path == before.path);
            NX_CHECK(parsed_config.rotation == before.rotation);
            NX_CHECK(parsed_config.max_spans_per_window == before.max_spans_per_window);
            NX_CHECK(parsed_config.window_ns == before.window_ns);
        }

        // The live sink's diagnostics surface.
        tracing::JsonFileTraceSink live{config};
        const std::string text = live.to_string();
        NX_CHECK(text == tracing::to_string(live));
        NX_CHECK(std::format("{}", live) == text);
        NX_CHECK(text.find("JsonFileTraceSink(") == 0U);
        const nlohmann::json j_live = live;
        NX_CHECK(j_live.at("type").get<std::string>() == "JsonFileTraceSink");
    }
}

void test_sinks_chrome() {
    // ---- ChromeTraceSink (task 7.3) ---------------------------------------
    {
        const std::string path = std::string(sink_tmp_dir) + "/chrome-trace.json";
        static_cast<void>(::unlink(path.c_str()));
        tracing::ChromeTraceSinkConfig config{};
        config.path = path;
        tracing::ChromeTraceSink sink{config};
        NX_CHECK(sink.start().has_value());

        // A parent and a child (same trace, child parents to the parent) —
        // the same-thread causal chain; plus one later span for relative ts.
        tracing::SpanRecord parent = crafted_record(10'000'000'000ULL, "parent-span");
        parent.parent_span_id = tracing::SpanId{}; // root
        tracing::SpanRecord child = crafted_record(10'000'500'000ULL, "child-span");
        child.trace_id = parent.trace_id;
        child.parent_span_id = parent.span_id;
        tracing::SpanRecord later = crafted_record(10'006'000'000ULL, "later-span");
        NX_CHECK(sink.on_span(parent).has_value());
        NX_CHECK(sink.on_span(child).has_value());
        NX_CHECK(sink.on_span(later).has_value());
        NX_CHECK(sink.flush().has_value());
        sink.stop();

        // The whole file parses as ONE JSON object (header/footer exact).
        const std::string content = read_file(path.c_str());
        NX_CHECK(content.find("{\"traceEvents\":[") == 0U);   // starts with header
        NX_CHECK(content.rfind("]}") == content.size() - 3U); // ends with ]}
        const nlohmann::json trace = nlohmann::json::parse(content);
        NX_CHECK(trace.is_object());
        const nlohmann::json& events = trace.at("traceEvents");
        NX_CHECK(events.is_array() && events.size() == 3); // one event per span

        // Complete-event fields: ph/name/cat/ts/dur/pid/tid/args.
        const nlohmann::json& first = events[0];
        NX_CHECK(first.at("ph").get<std::string>() == "X");
        NX_CHECK(first.at("name").get<std::string>() == "parent-span");
        NX_CHECK(first.at("cat").get<std::string>() == "Server");
        NX_CHECK(first.at("ts").get<std::uint64_t>() == 0U);  // first event ts == 0
        NX_CHECK(first.at("dur").get<std::uint64_t>() == 1U); // 1500 ns → 1 µs trunc
        NX_CHECK(first.at("pid").get<std::uint64_t>() == static_cast<std::uint64_t>(::getpid()));
        NX_CHECK(first.at("tid").get<std::uint64_t>() == 4242);
        const nlohmann::json& args = first.at("args");
        NX_CHECK(args.at("trace_id").get<std::string>() == "0123456789abcdef");
        NX_CHECK(args.at("span_id").get<std::string>() == "fedcba9876543210");
        NX_CHECK(args.at("parent_span_id").get<std::string>() == "0000000000000000");
        NX_CHECK(args.at("retries").get<std::int64_t>() == 3);
        NX_CHECK(args.at("ratio").get<double>() == 0.15625);
        NX_CHECK(args.at("ok").get<bool>());
        NX_CHECK(args.at("tag").get<std::string>() == "hil");

        // Later events: non-negative relative ts, dur == end-minus-start at
        // µs truncation, cross-event id coherence (the child carries the
        // parent's trace and span ids in args).
        const nlohmann::json& second = events[1];
        const std::uint64_t child_ts = second.at("ts").get<std::uint64_t>();
        const std::uint64_t later_ts = events[2].at("ts").get<std::uint64_t>();
        NX_CHECK(child_ts == 500U); // (10'000'500'000 - 10'000'000'000)/1000
        NX_CHECK(later_ts == 6'000U);
        NX_CHECK(later_ts >= child_ts);
        NX_CHECK(second.at("args").at("trace_id").get<std::string>() ==
                 first.at("args").at("trace_id").get<std::string>());
        NX_CHECK(second.at("args").at("parent_span_id").get<std::string>() ==
                 first.at("args").at("span_id").get<std::string>());
        NX_CHECK(events[2].at("dur").get<std::uint64_t>() == 1U);

        // Config trio + live diagnostics + unopenable path.
        const tracing::ChromeTraceSinkConfig chrome_config{"/tmp/t.json"};
        NX_CHECK(chrome_config.to_string() == tracing::to_string(chrome_config));
        NX_CHECK(std::format("{}", chrome_config) == chrome_config.to_string());
        const nlohmann::json j_cfg = chrome_config;
        tracing::ChromeTraceSinkConfig cfg_back{};
        nlohmann::from_json(j_cfg, cfg_back);
        NX_CHECK(cfg_back.path == "/tmp/t.json");
        tracing::ChromeTraceSinkConfig hostile_back{"/keep.json"};
        for (const nlohmann::json& bad : {nlohmann::json(1), nlohmann::json{1},
                                          nlohmann::json(nullptr), nlohmann::json{{"path", 3}}}) {
            nlohmann::from_json(bad, hostile_back);
            NX_CHECK(hostile_back.path == "/keep.json");
        }
        const std::string live_text = sink.to_string(); // closed after stop()
        NX_CHECK(live_text == tracing::to_string(sink));
        NX_CHECK(std::format("{}", sink) == live_text);
        const nlohmann::json j_live = sink;
        NX_CHECK(j_live.at("type").get<std::string>() == "ChromeTraceSink");

        tracing::ChromeTraceSinkConfig bad_config{};
        bad_config.path = "/tmp/opencode/nexigen-tracing-tests/missing-dir/t.json";
        tracing::ChromeTraceSink bad_sink{bad_config};
        const core::Status bad_start = bad_sink.start();
        NX_CHECK(!bad_start.has_value());
        NX_CHECK(bad_start.error() == core::ErrorCode::SystemError);
    }
}

template <typename HourMidUtc, typename MonoForUtc>
void test_sinks_composite(const HourMidUtc& hour_mid_utc, const MonoForUtc& mono_for_utc) {
    // ---- CompositeTraceSink (task 7.4) ------------------------------------
    {
        // Every member receives the same record through mixed sink types:
        // console line in the pipe + JSON object in the file + null.
        int fds[2] = {-1, -1};
        NX_CHECK(::pipe(fds) == 0);
        const std::string path = std::string(sink_tmp_dir) + "/composite.jsonl";
        static_cast<void>(::unlink(path.c_str()));
        tracing::JsonFileTraceSinkConfig file_config{};
        file_config.path = path;
        tracing::CompositeTraceSink<tracing::ConsoleTraceSink, tracing::JsonFileTraceSink,
                                    tracing::NullTraceSink>
            composite{tracing::ConsoleTraceSink{fds[1]}, tracing::JsonFileTraceSink{file_config},
                      tracing::NullTraceSink{}};
        NX_CHECK(composite.start().has_value());
        NX_CHECK(composite.on_span(crafted_record(mono_for_utc(hour_mid_utc(0)), "fan-out"))
                     .has_value());
        NX_CHECK(composite.flush().has_value());
        composite.stop();
        // The console fixture shows the span's line...
        char pipe_buffer[2048];
        std::size_t total = 0;
        while (total == 0U) {
            const ssize_t got = ::read(fds[0], pipe_buffer + total, sizeof(pipe_buffer) - total);
            if (got <= 0) {
                break;
            }
            total += static_cast<std::size_t>(got);
        }
        NX_CHECK(std::string_view(pipe_buffer, total).find("fan-out") != std::string_view::npos);
        // ...and the file contains the span's JSON object.
        const std::string file_content = read_file(path.c_str());
        NX_CHECK(file_content.find("fan-out") != std::string::npos);
        static_cast<void>(::close(fds[0]));
        static_cast<void>(::close(fds[1]));

        // A failing FIRST member does not starve the rest; the composite
        // surfaces the first error after all members were attempted.
        tracing::CompositeTraceSink<FailingOnSpanSink, CountingSink> mixed{FailingOnSpanSink{},
                                                                           CountingSink{}};
        NX_CHECK(mixed.start().has_value());
        const core::Status failed =
            mixed.on_span(crafted_record(mono_for_utc(hour_mid_utc(0)), "no-starve"));
        NX_CHECK(!failed.has_value());
        NX_CHECK(failed.error() == core::ErrorCode::SystemError);
        NX_CHECK(mixed.get<1>().deliveries == 1); // the second member received it
        NX_CHECK(mixed.flush().has_value());      // aggregated flush
        mixed.stop();

        // Typed/indexed access identity + the diagnostics surface.
        // A duplicated member type is legal for INDEX access (distinct
        // members); the TYPE access on it is a compile error (pinned by the
        // documented scratch-TU check at the top of this suite).
        tracing::CompositeTraceSink<tracing::NullTraceSink, tracing::NullTraceSink> dupes{};
        NX_CHECK(&dupes.get<0>() != &dupes.get<1>()); // two distinct members
        static_assert(tracing::CompositeTraceSink<tracing::NullTraceSink>::size == 1);
        tracing::CompositeTraceSink<tracing::NullTraceSink> single{tracing::NullTraceSink{}};
        NX_CHECK(&single.get<0>() == &single.get<tracing::NullTraceSink>());
        const std::string text = single.to_string();
        NX_CHECK(text == tracing::to_string(single));
        NX_CHECK(std::format("{}", single) == text);
        NX_CHECK(text.find("CompositeTraceSink(members=1") == 0U);
        const nlohmann::json j = single;
        NX_CHECK(j.at("type").get<std::string>() == "CompositeTraceSink");
        NX_CHECK(j.at("members").get<int>() == 1);
        // Out-of-range index / absent type fail at compile time: pinned by
        // the documented scratch-TU check at the top of this suite.
    }
}

// --- compile-time pins (task 6.2 / spec tracing/tracer-provider
// "Factory-only construction is pinned at compile time" and
// tracing/tracer-concepts): guards, tracers and the passkey are NOT
// constructible outside the factory hierarchy; the concrete Tracer
// satisfies the concept.

} // namespace

void suite_sinks() {
    run_suite("sinks", test_sinks);
}
