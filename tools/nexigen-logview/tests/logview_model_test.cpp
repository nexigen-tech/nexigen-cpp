// Headless tests for the logview model layer: no OpenGL, no window.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <malloc.h>
#include <string>
#include <string_view>
#include <vector>

#include <sched.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/civil_time.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/logging/log_frame.hpp>

#include "backend/capture_thread.hpp"
#include "export_writer.hpp"
#include "filter.hpp"
#include "find_state.hpp"
#include "importer.hpp"
#include "json_escape.hpp"
#include "log_view_model.hpp"
#include "logview_config.hpp"
#include "minimap.hpp"
#include "record_ring.hpp"
#include "resource_guard.hpp"
#include "time_formatter.hpp"
#include "ui/logview_window.hpp"
#include "cli.hpp"
#include "ui_actions.hpp"
#include "support/test_time_base.hpp"

using namespace nexigen::core;
using namespace nexigen::logview;
namespace core = nexigen::core;
namespace logging = nexigen::logging;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* expression, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    }
}

#define NX_CHECK(condition) check((condition), #condition, __LINE__)

std::string temp_path(std::string_view suffix) {
    return std::string("/tmp/opencode/logview-") + std::to_string(::getpid()) + "-" +
           std::string(suffix);
}

logging::LogFrame make_frame(std::uint64_t stamp, std::string_view app, core::LogLevel level,
                             std::string_view text, std::uint32_t dropped = 0) {
    logging::LogFrame frame{};
    frame.timestamp_ns = stamp;
    frame.log_level = level;
    frame.dropped_counter = dropped;
    frame.thread_id = 7;
    frame.pid = 42;
    const std::size_t app_size = std::min(app.size(), sizeof(frame.app_id));
    std::memcpy(frame.app_id, app.data(), app_size);
    const std::size_t text_size = std::min(text.size(), sizeof(frame.payload));
    std::memcpy(frame.payload, text.data(), text_size);
    frame.payload_size = static_cast<std::uint16_t>(text_size);
    return frame;
}

template <class T> void check_enum(T value) {
    T parsed{};
    NX_CHECK(from_string(to_string(value), parsed));
    NX_CHECK(parsed == value);
    nlohmann::json json = value;
    T from_json_value{};
    from_json(json, from_json_value);
    NX_CHECK(from_json_value == value);
    NX_CHECK(std::format("{}", value) == to_string(value));
}

void suite_cli_options() {
    CliOptions options{};
    options.ui_scale = 2.0F;
    NX_CHECK(options.to_string().find("scale=2.00") != std::string::npos);
    UiOptions ui{};
    ui.ui_scale = 1.5F;
    NX_CHECK(ui.to_string().find("scale=1.50") != std::string::npos);
}

void suite_vocabulary() {
    check_enum(RecordKind::Frame);
    check_enum(RecordKind::TransportGap);
    check_enum(RecordKind::IngressOverflow);
    check_enum(RecordKind::PublisherDrop);
    check_enum(TimestampMode::Absolute);
    check_enum(TimestampMode::RelativeT0);
    check_enum(DeltaMode::PerApplication);
    check_enum(ColorMode::Criticality);
    check_enum(ResourceMode::Eco);
    check_enum(EcoTrigger::Storm);
    check_enum(ExportFormat::Csv);
    NX_CHECK(is_synthetic(RecordKind::TransportGap));
    NX_CHECK(!is_synthetic(RecordKind::Frame));
}

void suite_app_registry() {
    AppRegistry registry;
    const std::size_t first = registry.observe("Engine", 100);
    NX_CHECK(first < AppRegistry::capacity);
    NX_CHECK(registry.at(first).record_count == 1);
    const std::size_t again = registry.observe("Engine", 250);
    NX_CHECK(again == first);
    NX_CHECK(registry.at(first).record_count == 2);
    NX_CHECK(registry.at(first).last_stamp_ns == 250);
    const std::size_t second = registry.observe("Viz", 300);
    NX_CHECK(second < AppRegistry::capacity);
    NX_CHECK(registry.at(second).color_index != registry.at(first).color_index);
    const std::string long_name = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const std::size_t truncated = registry.observe(long_name, 400);
    NX_CHECK(truncated < AppRegistry::capacity);
    NX_CHECK(registry.at(truncated).text() == std::string_view(long_name).substr(0, 16));
    registry.on_evict(first);
    NX_CHECK(registry.at(first).record_count == 1);
    NX_CHECK(registry.to_string().find("size=3") != std::string::npos);
}

void suite_record_ring() {
    NX_CHECK(RecordRing::capacity_for_budget(256ULL * 1024ULL * 1024ULL) == 524288U);
    NX_CHECK(RecordRing::capacity_for_budget(384ULL * 1024ULL * 1024ULL) == 1048576U);
    NX_CHECK(!RecordRing::create(3).has_value());

    auto ring_result = RecordRing::create(8);
    NX_CHECK(ring_result.has_value());
    if (!ring_result.has_value()) {
        return;
    }
    RecordRing ring = std::move(*ring_result);
    for (std::uint64_t i = 0; i < 10; ++i) {
        ring.push_frame(make_frame(1'000U + i * 10U, "A",
                                   i == 9 ? core::LogLevel::Error : core::LogLevel::Info,
                                   "message"));
    }
    NX_CHECK(ring.head() == 2U);
    NX_CHECK(ring.tail() == 10U);
    NX_CHECK(ring.size() == 8U);
    NX_CHECK(ring.evictions() == 2U);
    NX_CHECK(!ring.find(0U).has_value());
    NX_CHECK(ring.find(2U).has_value());
    NX_CHECK(ring.find(9U)->prev_same_app_ns == 10);
    for (std::uint64_t i = 0; i < 8; ++i) {
        ring.push_frame(make_frame(2'000U + i, "A", core::LogLevel::Info, "more"));
    }
    ring.clear();
    NX_CHECK(ring.size() == 0U);
    NX_CHECK(ring.apps().size() == 0U);

    static auto fail_map = [](std::size_t) -> void* { return nullptr; };
    const auto failed = RecordRing::create(8, fail_map);
    NX_CHECK(!failed.has_value());
    if (!failed.has_value()) {
        NX_CHECK(failed.error() == ErrorCode::SystemError);
    }
}

void suite_filter() {
    auto ring_result = RecordRing::create(32);
    NX_CHECK(ring_result.has_value());
    if (!ring_result.has_value()) {
        return;
    }
    RecordRing ring = std::move(*ring_result);
    FilterIndex filter;
    filter.init(ring.capacity(), ring.apps());
    const HistorySource source = HistorySource::live(ring);
    for (std::uint64_t i = 0; i < 10; ++i) {
        const auto seq = ring.push_frame(
            make_frame(i, "App", i % 2 == 0 ? core::LogLevel::Info : core::LogLevel::Error,
                       i % 2 == 0 ? "even" : "odd"));
        filter.append_if_visible(source, seq);
    }
    NX_CHECK(filter.visible_count() == 10U);
    FilterCriteria criteria{};
    criteria.levels[static_cast<std::size_t>(core::LogLevel::Error)] = false;
    NX_CHECK(criteria.matches(make_frame(0, "App", core::LogLevel::Info, "x"), ring.apps()));
    NX_CHECK(!criteria.matches(make_frame(0, "App", core::LogLevel::Error, "x"), ring.apps()));
    const std::size_t app_slot = ring.apps().find("App");
    NX_CHECK(app_slot < AppRegistry::capacity);
    if (app_slot < AppRegistry::capacity) {
        criteria.apps[app_slot] = false;
        criteria.app_filter_active = true;
        NX_CHECK(!criteria.matches(make_frame(0, "App", core::LogLevel::Info, "x"), ring.apps()));
    }
    filter.criteria().text = "even";
    filter.begin_rebuild(source);
    filter.step_rebuild(source, 3);
    NX_CHECK(filter.rebuilding());
    filter.step_rebuild(source, 100);
    NX_CHECK(!filter.rebuilding());
    NX_CHECK(filter.visible_count() == 5U);
    NX_CHECK(filter.visible_at(0) == 0U);
    const auto appended = ring.push_frame(make_frame(10, "App", core::LogLevel::Info, "even"));
    filter.append_if_visible(source, appended);
    NX_CHECK(filter.visible_count() == 6U);
    filter.clear();
    NX_CHECK(filter.visible_count() == 0U);

    filter.criteria().text.clear();
    filter.begin_rebuild(source);
    filter.step_rebuild(source, 100);
    NX_CHECK(filter.visible_count() == ring.size());
    // Evict while a rebuild is in progress: stale numbers must be skipped.
    filter.criteria().text = "even";
    filter.begin_rebuild(source);
    for (std::uint64_t i = 0; i < 40; ++i) {
        static_cast<void>(ring.push_frame(
            make_frame(100 + i, "App", core::LogLevel::Info, i % 2 == 0 ? "even" : "odd")));
    }
    filter.step_rebuild(source, 100);
    NX_CHECK(!filter.rebuilding());
    for (std::size_t i = 0; i < filter.visible_count(); ++i) {
        NX_CHECK(ring.find(filter.visible_at(i)).has_value());
    }

    // Synthetic gap records obey the same text/level criteria as frames.
    const std::uint64_t gap_seq = ring.push_gap(RecordKind::TransportGap, 3U, 9'000U);
    FilterCriteria gap_criteria{};
    gap_criteria.text = "gap";
    filter.criteria() = gap_criteria;
    filter.begin_rebuild(source);
    filter.step_rebuild(source, 10'000U);
    bool gap_visible = false;
    for (std::size_t i = 0; i < filter.visible_count(); ++i) {
        gap_visible = gap_visible || filter.visible_at(i) == gap_seq;
    }
    NX_CHECK(gap_visible);
    gap_criteria.text = "absent";
    filter.criteria() = gap_criteria;
    filter.begin_rebuild(source);
    filter.step_rebuild(source, 10'000U);
    gap_visible = false;
    for (std::size_t i = 0; i < filter.visible_count(); ++i) {
        gap_visible = gap_visible || filter.visible_at(i) == gap_seq;
    }
    NX_CHECK(!gap_visible);
    gap_criteria.text.clear();
    gap_criteria.levels[static_cast<std::size_t>(core::LogLevel::Warning)] = false;
    filter.criteria() = gap_criteria;
    filter.begin_rebuild(source);
    filter.step_rebuild(source, 10'000U);
    gap_visible = false;
    for (std::size_t i = 0; i < filter.visible_count(); ++i) {
        gap_visible = gap_visible || filter.visible_at(i) == gap_seq;
    }
    NX_CHECK(!gap_visible);
}

void suite_find() {
    auto ring_result = RecordRing::create(64);
    NX_CHECK(ring_result.has_value());
    if (!ring_result.has_value()) {
        return;
    }
    RecordRing ring = std::move(*ring_result);
    FilterIndex filter;
    filter.init(ring.capacity(), ring.apps());
    const HistorySource source = HistorySource::live(ring);
    for (std::uint64_t i = 0; i < 20; ++i) {
        const auto seq = ring.push_frame(
            make_frame(i, "App", core::LogLevel::Info, i % 5 == 0 ? "needle here" : "haystack"));
        filter.append_if_visible(source, seq);
    }
    FindState find;
    find.begin("needle", filter);
    NX_CHECK(find.active());
    find.step(filter, source, 3);
    NX_CHECK(find.active());
    find.step(filter, source, 100);
    NX_CHECK(!find.active());
    NX_CHECK(find.count() == 4U);
    NX_CHECK(find.contains(0U));
    NX_CHECK(!find.contains(1U));
    NX_CHECK(find.next() == 0U);
    NX_CHECK(find.next() == 5U);
    NX_CHECK(find.previous() == 0U);
    NX_CHECK(find.previous() == 15U);
    NX_CHECK(find.to_string().find("matches=4") != std::string::npos);
    find.begin("absent", filter);
    find.step(filter, source, 100);
    NX_CHECK(find.count() == 0U);
    NX_CHECK(!find.next().has_value());
    NX_CHECK(!find.previous().has_value());
}

void suite_time() {
    TimeContext context{};
    context.utc_offset_ns = 1'000'000'000LL;
    FormattedTime absolute =
        TimeFormatter::format_timestamp(5'000'000'000ULL, TimestampMode::Absolute, context);
    NX_CHECK(std::string(absolute.text.data()) == "1970-01-01T00:00:06.000000000Z");
    FormattedTime boot =
        TimeFormatter::format_timestamp(5'000'000'000ULL, TimestampMode::SinceBoot, context);
    NX_CHECK(std::string(boot.text.data()) == "00:00:05.000000000");
    context.set_anchor(5'500'000'000LL);
    FormattedTime relative =
        TimeFormatter::format_timestamp(5'000'000'000ULL, TimestampMode::RelativeT0, context);
    NX_CHECK(relative.negative);
    NX_CHECK(std::string(relative.text.data()) == "-500.000 ms");
    const FormattedTime negative = TimeFormatter::format_delta(-1'500);
    NX_CHECK(negative.negative);
    NX_CHECK(std::string(negative.text.data()) == "-1.500 us");
    NX_CHECK(TimeFormatter::is_anomalous(60'000'000, 50'000'000));
    NX_CHECK(!TimeFormatter::is_anomalous(-40'000'000, 50'000'000));
    context.set_anchor(6'000'000'000LL);
    const FormattedTime replaced =
        TimeFormatter::format_timestamp(5'500'000'000ULL, TimestampMode::RelativeT0, context);
    NX_CHECK(std::string(replaced.text.data()) == "-500.000 ms");
    context.clear_anchor();
    const FormattedTime cleared =
        TimeFormatter::format_timestamp(5'500'000'000ULL, TimestampMode::RelativeT0, context);
    NX_CHECK(std::string(cleared.text.data()) == "-");
    NX_CHECK(TimeFormatter::magnitude(std::numeric_limits<std::int64_t>::min()) > 0U);
}

void suite_time_base() {
    using nexigen::chrono::Timestamp;
    const auto now = Timestamp::now();
    NX_CHECK(now.has_value());
    if (!now.has_value()) {
        return;
    }
    const std::string calibrated =
        nexigen::tests::publish_test_time_base("logview-calibrated", 1'000'000'000LL, true);
    NX_CHECK(!calibrated.empty());
    if (!calibrated.empty()) {
        TimeContext context{};
        context.time_base_attached = true;
        const FormattedTime formatted =
            TimeFormatter::format_timestamp(now->nanoseconds(), TimestampMode::Absolute, context);
        char expected[31]{};
        nexigen::chrono::format_utc_iso8601_ns(
            static_cast<std::int64_t>(now->nanoseconds()) + 1'000'000'000LL, expected);
        NX_CHECK(formatted.calibrated);
        NX_CHECK(!formatted.unknown);
        NX_CHECK(std::string(formatted.text.data()) == expected);
    }
    nexigen::tests::detach_test_time_base();

    const std::string uncalibrated =
        nexigen::tests::publish_test_time_base("logview-uncalibrated", 0, false);
    NX_CHECK(!uncalibrated.empty());
    if (!uncalibrated.empty()) {
        TimeContext context{};
        context.time_base_attached = true;
        const FormattedTime formatted =
            TimeFormatter::format_timestamp(now->nanoseconds(), TimestampMode::Absolute, context);
        NX_CHECK(!formatted.calibrated);
        NX_CHECK(formatted.unknown);
        NX_CHECK(std::string(formatted.text.data()) == "no time base");
    }
    nexigen::tests::detach_test_time_base();
}

void suite_colors() {
    ColorManager colors;
    const LevelStyle fatal = colors.level(core::LogLevel::Fatal);
    NX_CHECK(fatal.background.a > 0.5F);
    NX_CHECK(colors.app_color(0).r != colors.app_color(1).r ||
             colors.app_color(0).g != colors.app_color(1).g);
    colors.level(core::LogLevel::Error).text = Rgba{0.1F, 0.2F, 0.3F, 1.0F};
    colors.set_mode(ColorMode::Criticality);
    const nlohmann::json json = colors;
    ColorManager restored;
    from_json(json, restored);
    NX_CHECK(restored.mode() == ColorMode::Criticality);
    NX_CHECK(restored.level(core::LogLevel::Error).text.b > 0.29F);
    restored.reset();
    NX_CHECK(restored.mode() == ColorMode::Combined);
    NX_CHECK(std::format("{}", restored).find("ColorManager") != std::string::npos);
    NX_CHECK(std::format("{}", Rgba{0.5F, 0.25F, 0.125F, 1.0F}).find("Rgba") != std::string::npos);
}

void suite_minimap() {
    Minimap minimap;
    minimap.on_ingest(5, core::LogLevel::Error);
    minimap.on_ingest(6, core::LogLevel::Fatal);
    minimap.on_bookmark(5, true);
    NX_CHECK(minimap.at(5).errors == 1U);
    NX_CHECK(minimap.at(6).fatals == 1U);
    NX_CHECK(minimap.at(5).bookmarks == 1U);
    minimap.on_evict(5, core::LogLevel::Error, true);
    NX_CHECK(minimap.at(5).errors == 0U);
    NX_CHECK(minimap.at(5).bookmarks == 0U);
}

void suite_export_import() {
    NX_CHECK(json_escape(std::string_view("a\"b\\c\nd")) == "a\\\"b\\\\c\\u000a d" ||
             json_escape(std::string_view("a\"b\\c\nd")).find("\\u000a") != std::string::npos);
    const std::string path = temp_path("export.jsonl");
    auto ring_result = RecordRing::create(64);
    NX_CHECK(ring_result.has_value());
    if (!ring_result.has_value()) {
        return;
    }
    RecordRing ring = std::move(*ring_result);
    logging::LogFrame frame =
        make_frame(1'000, "App", core::LogLevel::Error, "quote=\" backslash=\\ newline=\n end");
    const std::uint64_t seq = ring.push_frame(frame);
    ExportWriter writer;
    NX_CHECK(writer.start(path, ExportFormat::Jsonl, 1, TimeContext{}).has_value());
    ExportRecord record{};
    record.frame = frame;
    record.seq = seq;
    record.kind = RecordKind::Frame;
    NX_CHECK(writer.try_enqueue(record));
    const ExportStats stats = writer.finish();
    NX_CHECK(stats.written == 1U);
    NX_CHECK(!stats.failed);

    auto imported_ring_result = RecordRing::create(64);
    NX_CHECK(imported_ring_result.has_value());
    if (imported_ring_result.has_value()) {
        RecordRing imported_ring = std::move(*imported_ring_result);
        ImportStats import_stats{};
        NX_CHECK(import_file(path, imported_ring, import_stats).has_value());
        NX_CHECK(import_stats.imported == 1U);
        NX_CHECK(import_stats.malformed == 0U);
        NX_CHECK(imported_ring.size() == 1U);
        if (const auto view = imported_ring.find(imported_ring.head()); view.has_value()) {
            NX_CHECK(view->frame->payload_text() == frame.payload_text());
            NX_CHECK(view->frame->app_id_text() == "App");
            NX_CHECK(view->frame->log_level == core::LogLevel::Error);
        }
    }

    ImportStats plain_stats{};
    NX_CHECK(import_line("2026-01-01 ERROR plain line", ring, plain_stats));
    NX_CHECK(plain_stats.plain == 1U);
    ImportStats malformed_stats{};
    NX_CHECK(!import_line("{not json and no fields}", ring, malformed_stats));
    NX_CHECK(malformed_stats.malformed == 1U);
    ImportStats loose_stats{};
    NX_CHECK(import_line("{\"mono_ns\":10,\"app\":\"A\",\"level\":\"warn\","
                         "\"message\":\"unescaped \"quote\"",
                         ring, loose_stats));
    NX_CHECK(loose_stats.imported == 1U);
    // CSV quoting and TXT output.
    const std::string csv_path = temp_path("export.csv");
    ExportWriter csv_writer;
    NX_CHECK(csv_writer.start(csv_path, ExportFormat::Csv, 1, TimeContext{}).has_value());
    ExportRecord csv_record{};
    csv_record.frame =
        make_frame(2'000, "App", core::LogLevel::Warning, "comma, quote=\" and newline=\n");
    csv_record.seq = 77;
    NX_CHECK(csv_writer.try_enqueue(csv_record));
    static_cast<void>(csv_writer.finish());
    {
        std::ifstream csv_file(csv_path);
        std::string line;
        bool found = false;
        while (std::getline(csv_file, line)) {
            if (line.find("comma") != std::string::npos) {
                found = true;
                NX_CHECK(line.find("\"\"") != std::string::npos);
            }
        }
        NX_CHECK(found);
    }
    const std::string txt_path = temp_path("export.txt");
    ExportWriter txt_writer;
    NX_CHECK(txt_writer.start(txt_path, ExportFormat::Txt, 1, TimeContext{}).has_value());
    NX_CHECK(txt_writer.try_enqueue(csv_record));
    static_cast<void>(txt_writer.finish());
    {
        std::ifstream txt_file(txt_path);
        std::string line;
        std::getline(txt_file, line);
        NX_CHECK(line.find("seq") != std::string::npos);
        std::getline(txt_file, line);
        NX_CHECK(line.find("comma") != std::string::npos);
    }
    ExportWriter cancelled;
    NX_CHECK(cancelled.start(temp_path("cancel.jsonl"), ExportFormat::Jsonl, 1, TimeContext{})
                 .has_value());
    cancelled.cancel();
    const ExportStats cancelled_stats = cancelled.stats();
    NX_CHECK(cancelled_stats.cancelled);
    static_cast<void>(std::remove(csv_path.c_str()));
    static_cast<void>(std::remove(txt_path.c_str()));

    // Mixed boot sessions are surfaced, not silently compared.
    const std::string mixed_path = temp_path("mixed.jsonl");
    {
        std::ofstream file(mixed_path, std::ios::trunc);
        file << "{\"mono_ns\":1,\"boot\":\"aaaaaaaaaaaaaaaa\",\"app\":\"A\","
                "\"level\":\"info\",\"message\":\"one\"}\n";
        file << "{\"mono_ns\":2,\"boot\":\"bbbbbbbbbbbbbbbb\",\"app\":\"A\","
                "\"level\":\"info\",\"message\":\"two\"}\n";
    }
    auto mixed_ring_result = RecordRing::create(64);
    NX_CHECK(mixed_ring_result.has_value());
    if (mixed_ring_result.has_value()) {
        ImportStats mixed_stats{};
        NX_CHECK(import_file(mixed_path, *mixed_ring_result, mixed_stats).has_value());
        NX_CHECK(mixed_stats.mixed_boot);
        NX_CHECK(mixed_stats.imported == 2U);
    }
    static_cast<void>(std::remove(path.c_str()));
    static_cast<void>(std::remove(mixed_path.c_str()));
}

void suite_config() {
    LogViewConfig config;
    config.service_name = "Nexigen/Test/Logview";
    config.max_memory_mb = 384;
    config.timestamp_mode = TimestampMode::SinceBoot;
    config.delta_mode = DeltaMode::FromAnchor;
    config.level_visible[2] = false;
    config.hidden_apps.push_back("Engine");
    config.colors.level(core::LogLevel::Warning).text = Rgba{0.4F, 0.5F, 0.6F, 1.0F};
    const nlohmann::json json = config;
    LogViewConfig restored;
    from_json(json, restored);
    NX_CHECK(restored.service_name == config.service_name);
    NX_CHECK(restored.max_memory_mb == 384U);
    NX_CHECK(restored.timestamp_mode == TimestampMode::SinceBoot);
    NX_CHECK(restored.hidden_apps.size() == 1U);
    NX_CHECK(std::format("{}", restored).find("service=Nexigen/Test/Logview") != std::string::npos);

    ViewConfig view{};
    view.id = 7U;
    view.title = "All logs";
    view.source = "";
    view.levels[static_cast<std::size_t>(core::LogLevel::Debug)] = false;
    view.text = "needle";
    view.timestamp_mode = TimestampMode::RelativeT0;
    view.delta_mode = DeltaMode::FromAnchor;
    view.anomaly_threshold_ns = 42'000'000U;
    view.hidden_apps.push_back("Noisy");
    const nlohmann::json view_json = view;
    ViewConfig view_restored{};
    from_json(view_json, view_restored);
    NX_CHECK(view_restored.id == 7U);
    NX_CHECK(view_restored.title == "All logs");
    NX_CHECK(!view_restored.levels[static_cast<std::size_t>(core::LogLevel::Debug)]);
    NX_CHECK(view_restored.text == "needle");
    NX_CHECK(view_restored.timestamp_mode == TimestampMode::RelativeT0);
    NX_CHECK(view_restored.delta_mode == DeltaMode::FromAnchor);
    NX_CHECK(view_restored.anomaly_threshold_ns == 42'000'000U);
    NX_CHECK(view_restored.hidden_apps.size() == 1U);
    NX_CHECK(std::format("{}", view_restored) == view_restored.to_string());

    const std::string path = temp_path("config.json");
    NX_CHECK(save_config(path, config).has_value());
    LogViewConfig loaded;
    bool warning = true;
    NX_CHECK(load_config(path, loaded, warning).has_value());
    NX_CHECK(!warning);
    NX_CHECK(loaded.max_memory_mb == 384U);
    {
        std::ofstream bad(path, std::ios::trunc);
        bad << "{ definitely not json";
    }
    LogViewConfig corrupt;
    warning = false;
    NX_CHECK(!load_config(path, corrupt, warning).has_value());
    NX_CHECK(warning);
    static_cast<void>(std::remove(path.c_str()));

    LogViewConfig invalid;
    invalid.max_memory_mb = 1;
    NX_CHECK(!invalid.validate().has_value());
    invalid.max_memory_mb = 64;
    NX_CHECK(invalid.validate().has_value());
    NX_CHECK(!invalid.capture_thread.set_scheduler(SchedulingPolicy::Other, 3).has_value());
    NX_CHECK(invalid.validate().has_value());
}

void suite_resource_guard() {
    ResourceGuardConfig config{};
    config.cpu_limit_percent = 50;
    config.entry_windows = 3;
    config.exit_windows = 2;
    ResourceGuard guard(config);
    NX_CHECK(guard.observe(0, 0, 0) == ResourceMode::Normal);
    NX_CHECK(guard.observe(1'000'000'000ULL, 800'000'000ULL, 10) == ResourceMode::Normal);
    NX_CHECK(guard.observe(2'000'000'000ULL, 1'600'000'000ULL, 10) == ResourceMode::Normal);
    NX_CHECK(guard.observe(3'000'000'000ULL, 2'400'000'000ULL, 10) == ResourceMode::Eco);
    NX_CHECK(guard.trigger() == EcoTrigger::Cpu);
    NX_CHECK(guard.frame_interval_seconds() > 0.05);
    NX_CHECK(guard.observe(4'000'000'000ULL, 2'600'000'000ULL, 10) == ResourceMode::Eco);
    NX_CHECK(guard.observe(5'000'000'000ULL, 2'800'000'000ULL, 10) == ResourceMode::Normal);
    NX_CHECK(guard.trigger() == EcoTrigger::None);

    ResourceGuard storm;
    NX_CHECK(storm.observe(0, 0, 0) == ResourceMode::Normal);
    NX_CHECK(storm.observe(1'000'000'000ULL, 0, 200'000) == ResourceMode::Eco);
    NX_CHECK(storm.trigger() == EcoTrigger::Storm);
    NX_CHECK(storm.to_string().find("eco") != std::string::npos);
}

void suite_model() {
    LogViewConfig config;
    config.max_memory_mb = 64;
    config.service_name = "Nexigen/Test/Model";
    config.hidden_apps.push_back("Hidden");
    auto model_result = LogViewModel::create(64ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(model_result.has_value());
    if (!model_result.has_value()) {
        return;
    }
    std::unique_ptr<LogViewModel> model = std::move(*model_result);
    model->ingest_frame(make_frame(100, "Engine", core::LogLevel::Info, "first", 2), 5);
    NX_CHECK(model->counters().received == 1U);
    NX_CHECK(model->counters().synthetic == 2U);
    model->ingest_frame(make_frame(200, "Hidden", core::LogLevel::Info, "hidden"), 0);
    const std::size_t hidden_slot = model->ring().apps().find("Hidden");
    NX_CHECK(hidden_slot < AppRegistry::capacity);
    if (hidden_slot < AppRegistry::capacity) {
        NX_CHECK(!model->filter().criteria().apps[hidden_slot]);
    }
    model->set_paused(true);
    NX_CHECK(model->paused());
    const std::size_t paused_display = model->display_count();
    for (std::uint64_t i = 0; i < model->ring().capacity() + 10U; ++i) {
        model->ingest_frame(make_frame(1'000 + i, "Viz", core::LogLevel::Info, "stream"), 0);
    }
    NX_CHECK(model->new_since_pause() > 0U);
    NX_CHECK(model->overwritten_while_paused() > 0U);
    NX_CHECK(model->display_count() <= paused_display);
    model->set_paused(false);
    NX_CHECK(!model->paused());

    const std::uint64_t last = model->ring().tail() - 1U;
    LogView* focused = model->focused_view();
    NX_CHECK(focused != nullptr);
    model->select_only(last);
    NX_CHECK(focused->is_selected(last));
    NX_CHECK(model->toggle_bookmark(last));
    NX_CHECK(model->next_bookmark(std::nullopt).has_value());
    model->select_all_visible();
    NX_CHECK(focused->selected_count() == model->display_count());
    model->clear_selection();
    NX_CHECK(focused->selected_count() == 0U);

    const std::string export_path = temp_path("model-export.jsonl");
    model->select_only(last);
    CpuSet forbidden;
    NX_CHECK(forbidden.add(1023).has_value());
    ThreadSpec forbidden_spec;
    forbidden_spec.set_cpus(forbidden);
    model->config().export_thread = forbidden_spec;
    NX_CHECK(!model->start_export(export_path, ExportFormat::Jsonl).has_value());
    model->config().export_thread = ThreadSpec{};
    NX_CHECK(model->start_export(export_path, ExportFormat::Jsonl).has_value());
    while (model->exporting()) {
        model->step_export(64);
    }
    NX_CHECK(model->export_stats().written == 1U);
    static_cast<void>(std::remove(export_path.c_str()));

    model->ingest_frame(make_frame(2'000, "Warn", core::LogLevel::Warning, "warning"), 0);
    model->ingest_frame(make_frame(2'100, "Err", core::LogLevel::Error, "error"), 0);
    const std::uint64_t warning_seq = model->ring().tail() - 2U;
    model->select_only(warning_seq);
    NX_CHECK(model->toggle_bookmark(warning_seq));
    model->filter().criteria().text = "absent";
    model->begin_filter_rebuild();
    while (model->filter().rebuilding()) {
        model->step_filter_rebuild(1'000U);
    }
    NX_CHECK(focused->is_selected(warning_seq));
    NX_CHECK(focused->is_bookmarked(warning_seq));
    model->filter().criteria().text.clear();
    model->begin_filter_rebuild();
    while (model->filter().rebuilding()) {
        model->step_filter_rebuild(1'000U);
    }
    NX_CHECK(model->next_attention(std::nullopt).has_value());
    NX_CHECK(model->next_attention(warning_seq).has_value());
    NX_CHECK(model->previous_attention(model->ring().tail() - 1U).has_value());
    NX_CHECK(!model->restart_required());
    model->config().timestamp_mode = TimestampMode::SinceBoot;
    model->colors().level(core::LogLevel::Error).text = Rgba{0.2F, 0.3F, 0.4F, 1.0F};
    model->reset_configuration();
    NX_CHECK(model->restart_required());
    NX_CHECK(model->config().service_name == LogViewConfig{}.service_name);
    NX_CHECK(model->config().max_memory_mb == LogViewConfig{}.max_memory_mb);
    NX_CHECK(model->config().timestamp_mode == TimestampMode::Absolute);
    NX_CHECK(model->filter().criteria().text.empty());
    NX_CHECK(model->colors().mode() == ColorMode::Combined);
    NX_CHECK(model->colors().level(core::LogLevel::Error).text.r > 0.5F);

    const std::string long_message(211U, 'Z');
    model->ingest_frame(make_frame(3'000, "Trunc", core::LogLevel::Info, long_message), 0);
    model->select_only(model->ring().tail() - 1U);
    NX_CHECK(ui::selected_payload_truncated(*model));
    NX_CHECK(!ui::copy_selected(*model).empty());
    NX_CHECK(ui::open_requires_confirmation(true, model->ring().size()));
    NX_CHECK(!ui::open_requires_confirmation(false, model->ring().size()));

    const std::string open_path = temp_path("ui-open.jsonl");
    {
        std::ofstream file(open_path, std::ios::trunc);
        file << "{\"mono_ns\":1,\"boot\":\"aaaaaaaaaaaaaaaa\",\"app\":\"Open\","
                "\"level\":\"info\",\"message\":\"opened\"}\n";
    }
    NX_CHECK(ui::open_file(*model, open_path).has_value());
    while (model->importing()) {
        model->step_import(100U);
    }
    NX_CHECK(model->ring().size() == 1U);
    NX_CHECK(model->import_stats().imported == 1U);
    static_cast<void>(std::remove(open_path.c_str()));

    auto overflow_queue = std::make_unique<nexigen::core::SpscRingBuffer<IngressRecord, 65536>>();
    static_cast<void>(model->drain(*overflow_queue, 7U));
    NX_CHECK(model->counters().synthetic == 1U);
    if (const auto gap = model->ring().find(model->ring().tail() - 1U); gap.has_value()) {
        NX_CHECK(gap->kind == RecordKind::IngressOverflow);
        NX_CHECK(gap->gap_count == 7U);
    }

    model->begin_filter_rebuild();
    while (model->filter().rebuilding()) {
        model->step_filter_rebuild(1'000U);
    }
    model->clear_history();
    NX_CHECK(model->ring().size() == 0U);
    NX_CHECK(model->counters().received == 0U);
}

void suite_scale() {
    auto ring_result = RecordRing::create(524288);
    NX_CHECK(ring_result.has_value());
    if (!ring_result.has_value()) {
        return;
    }
    RecordRing ring = std::move(*ring_result);
    FilterIndex filter;
    filter.init(ring.capacity(), ring.apps());
    const HistorySource source = HistorySource::live(ring);
    const auto begin = std::chrono::steady_clock::now();
    constexpr std::uint64_t count = 500'000;
    for (std::uint64_t i = 0; i < count; ++i) {
        const char* app = (i % 4U) == 0U ? "Bench" : ((i % 4U) == 1U ? "Engine" : "Viz");
        const auto seq = ring.push_frame(make_frame(
            1'000'000 + i * 1'000U, app,
            (i % 1000U) == 0U ? core::LogLevel::Error : core::LogLevel::Info, "scale record"));
        filter.append_if_visible(source, seq);
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin);
    NX_CHECK(filter.visible_count() == count);
    NX_CHECK(ring.size() == count);
    NX_CHECK(ring.find(count / 2U).has_value());
    NX_CHECK(elapsed.count() < 20.0);
    TimeContext time{};
    const auto render_begin = std::chrono::steady_clock::now();
    constexpr std::size_t render_iterations = 200;
    for (std::size_t iteration = 0; iteration < render_iterations; ++iteration) {
        for (std::size_t row = 0; row < 60U; ++row) {
            const auto view = ring.find(filter.visible_at((row * 7919U) % count));
            NX_CHECK(view.has_value());
            if (view.has_value()) {
                const FormattedTime stamp = TimeFormatter::format_timestamp(
                    view->frame->timestamp_ns, TimestampMode::Absolute, time);
                const FormattedTime delta = TimeFormatter::format_delta(view->prev_same_app_ns);
                NX_CHECK(stamp.text[0] != '\0');
                NX_CHECK(delta.text[0] != '\0');
            }
        }
    }
    const auto render_elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - render_begin);
    const double per_frame_ms =
        render_elapsed.count() * 1000.0 / static_cast<double>(render_iterations);
    std::printf("visible-row render model: 60 rows in %.3f ms/frame\n", per_frame_ms);
    NX_CHECK(per_frame_ms < 3.0);
    std::printf("scale: %llu records in %.3f s (%.0f ns/record)\n",
                static_cast<unsigned long long>(count), elapsed.count(),
                elapsed.count() * 1'000'000'000.0 / static_cast<double>(count));
    filter.criteria().text = "scale";
    filter.begin_rebuild(source);
    while (filter.rebuilding()) {
        filter.step_rebuild(source, 20'000U);
    }
    NX_CHECK(filter.visible_count() == count);
}

void suite_allocation() {
    LogViewConfig config;
    config.max_memory_mb = 64;
    auto model_result = LogViewModel::create(64ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(model_result.has_value());
    if (!model_result.has_value()) {
        return;
    }
    std::unique_ptr<LogViewModel> model = std::move(*model_result);
    const logging::LogFrame frame =
        make_frame(1'000, "Alloc", core::LogLevel::Info, "no allocation on ingest");
    const std::size_t ingest_before = mallinfo2().uordblks;
    for (std::uint64_t i = 0; i < 1'000U; ++i) {
        model->ingest_frame(frame, 0);
    }
    const std::size_t ingest_after = mallinfo2().uordblks;
    NX_CHECK(ingest_after <= ingest_before + 4'096U);

    TimeContext time{};
    const std::size_t render_before = mallinfo2().uordblks;
    for (std::size_t iteration = 0; iteration < 200U; ++iteration) {
        for (std::size_t row = 0; row < 60U; ++row) {
            const auto view = model->ring().find(model->ring().head() + row);
            if (!view.has_value()) {
                continue;
            }
            const FormattedTime stamp = TimeFormatter::format_timestamp(
                view->frame->timestamp_ns, TimestampMode::Absolute, time);
            const FormattedTime delta = TimeFormatter::format_delta(view->prev_same_app_ns);
            NX_CHECK(stamp.text[0] != '\0');
            NX_CHECK(delta.text[0] != '\0');
        }
    }
    const std::size_t render_after = mallinfo2().uordblks;
    NX_CHECK(render_after <= render_before + 4'096U);
}

void suite_memory_budget() {
    auto small = RecordRing::create_for_budget(256ULL * 1024ULL * 1024ULL);
    NX_CHECK(small.has_value());
    if (small.has_value()) {
        NX_CHECK(small->capacity() == 524288U);
        NX_CHECK(small->mapped_bytes() + 65536ULL * sizeof(IngressRecord) <=
                 256ULL * 1024ULL * 1024ULL);
    }
    auto large = RecordRing::create_for_budget(384ULL * 1024ULL * 1024ULL);
    NX_CHECK(large.has_value());
    if (large.has_value()) {
        NX_CHECK(large->capacity() == 1048576U);
        NX_CHECK(large->mapped_bytes() + 65536ULL * sizeof(IngressRecord) <=
                 384ULL * 1024ULL * 1024ULL);
    }
}

void suite_import_session() {
    const std::string path = temp_path("session.jsonl");
    {
        std::ofstream file(path, std::ios::trunc);
        for (std::uint64_t i = 0; i < 5000U; ++i) {
            file << "{\"mono_ns\":" << (1000U + i) << ",\"app\":\"Bulk\",\"level\":\"info\","
                 << "\"message\":\"line " << i << "\"}\n";
        }
        file << "{not json without fields}\n";
    }
    auto session = ImportSession::open(path);
    NX_CHECK(session.has_value());
    auto ring = RecordRing::create(8192);
    NX_CHECK(ring.has_value());
    if (session.has_value() && ring.has_value()) {
        ImportStats stats{};
        int steps = 0;
        while (!(*session)->step(*ring, stats, 100U)) {
            ++steps;
            NX_CHECK((*session)->ratio() >= 0.0 && (*session)->ratio() <= 1.0);
            NX_CHECK(steps < 100);
        }
        NX_CHECK(stats.imported == 5000U);
        NX_CHECK(stats.malformed == 1U);
        NX_CHECK((*session)->done());
    }
    static_cast<void>(std::remove(path.c_str()));
}

void suite_thread_spec() {
    cpu_set_t set{};
    if (::sched_getaffinity(0, sizeof(set), &set) == 0) {
        std::vector<std::uint32_t> cpus;
        for (std::uint32_t cpu = 0; cpu < 64U; ++cpu) {
            if (CPU_ISSET(cpu, &set)) {
                cpus.push_back(cpu);
            }
        }
        if (cpus.size() >= 2U) {
            CpuSet requested;
            NX_CHECK(requested.add(cpus[0]).has_value());
            NX_CHECK(requested.add(cpus[1]).has_value());
            ThreadSpec spec;
            spec.set_cpus(requested);
            NX_CHECK(spec.set_name("lgview-test").has_value());
            spec.set_apply_mode(ThreadApplyMode::Required);
            ThreadApplyReport report{};
            NX_CHECK(apply_thread_spec_self(spec, report).has_value());
        } else {
            std::printf("thread spec affinity test skipped: fewer than two CPUs\n");
        }
    }
    {
        CpuSet forbidden;
        NX_CHECK(forbidden.add(1023).has_value());
        ThreadSpec spec;
        spec.set_cpus(forbidden);
        auto capture = std::make_unique<CaptureThread>("Nexigen/Test/InvalidAffinity", spec);
        NX_CHECK(!capture->start().has_value());
    }
    ThreadSpec invalid;
    NX_CHECK(!invalid.set_scheduler(SchedulingPolicy::Other, 4).has_value());
    NX_CHECK(!invalid.set_name("this-name-is-too-long").has_value());
}

void drain_view_rebuilds(LogViewModel& model) {
    for (;;) {
        model.step_filter_rebuilds(1'000'000U);
        bool rebuilding = false;
        for (std::size_t i = 0; i < model.view_count(); ++i) {
            const LogView* view = model.view_at(i);
            rebuilding = rebuilding || (view != nullptr && view->rebuilding());
        }
        if (!rebuilding) {
            return;
        }
    }
}

void suite_views() {
    LogViewConfig config;
    config.max_memory_mb = 64;
    auto model_result = LogViewModel::create(64ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(model_result.has_value());
    if (!model_result.has_value()) {
        return;
    }
    std::unique_ptr<LogViewModel> model = std::move(*model_result);
    NX_CHECK(model->view_count() == 1U);
    NX_CHECK(model->focused_view() != nullptr);
    NX_CHECK(model->focused_view()->kind() == ViewKind::All);

    for (std::uint64_t i = 0; i < 20; ++i) {
        model->ingest_frame(make_frame(1'000U + i * 10U, i % 2 == 0U ? "Engine" : "Viz",
                                       i % 5 == 0U ? core::LogLevel::Error : core::LogLevel::Info,
                                       i % 5 == 0U ? "needle" : "haystack"),
                            0U);
    }
    const auto engine = model->create_view(ViewKind::Source, "Engine");
    NX_CHECK(engine.has_value());
    const auto viz = model->create_view(ViewKind::Source, "Viz");
    NX_CHECK(viz.has_value());
    if (!engine.has_value() || !viz.has_value()) {
        return;
    }
    drain_view_rebuilds(*model);
    LogView* engine_view = model->view(*engine);
    LogView* viz_view = model->view(*viz);
    NX_CHECK(engine_view != nullptr);
    NX_CHECK(viz_view != nullptr);
    if (engine_view == nullptr || viz_view == nullptr) {
        return;
    }
    NX_CHECK(engine_view->kind() == ViewKind::Source);
    NX_CHECK(engine_view->source() == "Engine");
    NX_CHECK(engine_view->display_count() > 0U);
    bool only_engine = true;
    for (std::size_t i = 0; i < engine_view->display_count(); ++i) {
        const auto record = engine_view->history().find(engine_view->display_at(i));
        if (!record.has_value() || !record->is_frame() ||
            record->frame->app_id_text() != "Engine") {
            only_engine = false;
        }
    }
    NX_CHECK(only_engine);

    const std::size_t viz_visible = viz_view->display_count();
    engine_view->filter().criteria().levels[static_cast<std::size_t>(core::LogLevel::Error)] =
        false;
    model->begin_filter_rebuild(*engine_view);
    drain_view_rebuilds(*model);
    NX_CHECK(viz_view->display_count() == viz_visible);
    bool no_errors = true;
    for (std::size_t i = 0; i < engine_view->display_count(); ++i) {
        const auto record = engine_view->history().find(engine_view->display_at(i));
        if (record.has_value() && record->is_frame() &&
            record->frame->log_level == core::LogLevel::Error) {
            no_errors = false;
        }
    }
    NX_CHECK(no_errors);

    // The All view's source mask is local; source views keep their scope.
    LogView* all_view = model->view_at(0);
    NX_CHECK(all_view != nullptr);
    NX_CHECK(all_view->kind() == ViewKind::All);
    if (all_view != nullptr) {
        const std::size_t engine_slot = model->ring().apps().find("Engine");
        NX_CHECK(engine_slot < AppRegistry::capacity);
        const std::size_t all_before = all_view->display_count();
        const std::size_t viz_before = viz_view->display_count();
        all_view->filter().criteria().apps[engine_slot] = false;
        all_view->filter().criteria().app_filter_active = true;
        model->begin_filter_rebuild(*all_view);
        drain_view_rebuilds(*model);
        NX_CHECK(viz_view->display_count() == viz_before);
        NX_CHECK(all_view->display_count() < all_before);
        bool all_engine_hidden = true;
        for (std::size_t i = 0; i < all_view->display_count(); ++i) {
            const auto record = all_view->history().find(all_view->display_at(i));
            if (record.has_value() && record->is_frame() &&
                record->frame->app_id_text() == "Engine") {
                all_engine_hidden = false;
            }
        }
        NX_CHECK(all_engine_hidden);
        all_view->filter().criteria().apps[engine_slot] = true;
        all_view->filter().criteria().app_filter_active = false;
        model->begin_filter_rebuild(*all_view);
        drain_view_rebuilds(*model);
    }

    const std::uint64_t engine_seq = engine_view->display_at(0);
    engine_view->select_only(engine_seq);
    NX_CHECK(engine_view->is_selected(engine_seq));
    NX_CHECK(!viz_view->is_selected(engine_seq));
    NX_CHECK(engine_view->toggle_bookmark(engine_seq));
    NX_CHECK(!viz_view->is_bookmarked(engine_seq));
    engine_view->time().set_anchor(123'456);
    NX_CHECK(!viz_view->time().anchor_ns.has_value());

    NX_CHECK(ui::shortcuts_allowed(true, false));
    NX_CHECK(!ui::shortcuts_allowed(false, false));
    NX_CHECK(!ui::shortcuts_allowed(true, true));
    model->focus_view(*engine);
    NX_CHECK(ui::copy_selected(*model).find("Engine") != std::string::npos);
    viz_view->select_only(viz_view->display_at(0));
    model->focus_view(*viz);
    NX_CHECK(ui::copy_selected(*model).find("Viz") != std::string::npos);
    NX_CHECK(engine_view->find().matches().capacity() == 0U);
    NX_CHECK(engine_view->filter().capacity() == model->ring().capacity());

    // Selection is per view, but only one export writer may run.
    NX_CHECK(engine_view->display_count() > 0U);
    NX_CHECK(viz_view->display_count() > 0U);
    engine_view->select_only(engine_view->display_at(0));
    viz_view->select_only(viz_view->display_at(0));
    const std::string export_a = temp_path("view-a.jsonl");
    const std::string export_b = temp_path("view-b.jsonl");
    NX_CHECK(model->start_export(*engine, export_a, ExportFormat::Jsonl).has_value());
    NX_CHECK(!model->start_export(*viz, export_b, ExportFormat::Jsonl).has_value());
    while (model->exporting()) {
        model->step_export(16U);
    }
    NX_CHECK(model->export_stats().written == 1U);
    NX_CHECK(model->start_export(*viz, export_b, ExportFormat::Jsonl).has_value());
    while (model->exporting()) {
        model->step_export(16U);
    }
    NX_CHECK(model->export_stats().written == 1U);
    static_cast<void>(std::remove(export_a.c_str()));
    static_cast<void>(std::remove(export_b.c_str()));

    const auto live_clone = model->clone_view(*engine, CloneMode::Live, CloneOptions{});
    NX_CHECK(live_clone.has_value());
    if (live_clone.has_value()) {
        LogView* clone = model->view(*live_clone);
        NX_CHECK(clone != nullptr);
        NX_CHECK(clone->live());
        NX_CHECK(!clone->paused());
        if (clone != nullptr) {
            model->ingest_frame(make_frame(99'000U, "Engine", core::LogLevel::Info, "tail"), 0U);
            bool seen = false;
            for (std::size_t i = 0; i < clone->display_count(); ++i) {
                seen = seen || clone->display_at(i) == model->ring().tail() - 1U;
            }
            NX_CHECK(seen);
            NX_CHECK(clone->id() != engine_view->id());
            NX_CHECK(clone->source() == engine_view->source());
        }
    }

    // Limits: All, source views, and the live clone fill the LIVE budget.
    for (int i = 0; i < 4; ++i) {
        NX_CHECK(
            model->create_view(ViewKind::Source, std::string("S") + std::to_string(i)).has_value());
    }
    NX_CHECK(model->live_view_count() == 8U);
    NX_CHECK(!model->create_view(ViewKind::Source, "Overflow").has_value());
    const std::uint64_t first_slot = model->view_at(1)->id();
    NX_CHECK(model->close_view(first_slot).has_value());
    const std::size_t views_before_rejection = model->view_count();
    const ViewId focus_before_rejection = model->focused_id();
    model->set_view_index_budget(0U);
    NX_CHECK(!model->create_view(ViewKind::Source, "AllocFail").has_value());
    model->set_view_index_budget(static_cast<std::size_t>(-1));
    NX_CHECK(model->view_count() == views_before_rejection);
    NX_CHECK(model->focused_id() == focus_before_rejection);
    NX_CHECK(model->create_view(ViewKind::Source, "Reuse").has_value());
    NX_CHECK(model->live_view_count() == 8U);

    const std::size_t default_capacity =
        RecordRing::capacity_for_budget(256ULL * 1024ULL * 1024ULL);
    const std::size_t index_total = LogViewModel::index_bytes_for(default_capacity) * 8U;
    NX_CHECK(index_total >= 32ULL * 1024ULL * 1024ULL);
    NX_CHECK(index_total <= 34ULL * 1024ULL * 1024ULL);

    // Persisted definitions contain only LIVE views and round-trip their data.
    const std::vector<ViewConfig> definitions = model->view_configs();
    NX_CHECK(definitions.size() == 8U);
    const nlohmann::json json = definitions.front();
    ViewConfig restored{};
    from_json(json, restored);
    NX_CHECK(restored.id == definitions.front().id);
    NX_CHECK(restored.title == definitions.front().title);
    NX_CHECK(std::format("{}", restored) == restored.to_string());
}

void suite_paused_views() {
    LogViewConfig config;
    config.max_memory_mb = 64;
    auto model_result = LogViewModel::create(64ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(model_result.has_value());
    if (!model_result.has_value()) {
        return;
    }
    std::unique_ptr<LogViewModel> model = std::move(*model_result);
    const std::size_t history_records = (kMaxSnapshotRecordBytes / kSnapshotRecordBytes + 40U) * 3U;
    for (std::uint64_t i = 0; i < history_records; ++i) {
        model->ingest_frame(make_frame(1'000U + i, i % 3 == 0U ? "Engine" : "Viz",
                                       i % 10 == 0U ? core::LogLevel::Error : core::LogLevel::Info,
                                       i % 5 == 0U ? "needle here" : "haystack"),
                            0U);
    }
    model->ingest_gap(RecordKind::TransportGap, 4U, 9'000U);
    const auto engine = model->create_view(ViewKind::Source, "Engine");
    NX_CHECK(engine.has_value());
    if (!engine.has_value()) {
        return;
    }
    LogView* source = model->view(*engine);
    NX_CHECK(source != nullptr);
    if (source == nullptr) {
        return;
    }
    drain_view_rebuilds(*model);

    const std::uint64_t old_seq = model->ring().head();
    source->select_only(old_seq);
    NX_CHECK(source->toggle_bookmark(old_seq));
    source->time().set_anchor(1'000);
    source->filter().criteria().levels[static_cast<std::size_t>(core::LogLevel::Error)] = false;
    source->filter().criteria().text = "needle";
    model->begin_filter_rebuild(*source);
    drain_view_rebuilds(*model);
    model->begin_find(*source, "needle");
    while (source->find().active()) {
        static_cast<void>(source->find().step(source->filter(), source->history(), 1'000U));
    }
    NX_CHECK(source->find().count() > 0U);

    const auto paused = model->clone_view(*engine, CloneMode::Paused, CloneOptions{});
    NX_CHECK(paused.has_value());
    if (!paused.has_value()) {
        return;
    }
    LogView* snapshot = model->view(*paused);
    NX_CHECK(snapshot != nullptr);
    if (snapshot == nullptr) {
        return;
    }
    NX_CHECK(!snapshot->live());
    NX_CHECK(snapshot->paused());
    NX_CHECK(snapshot->frozen() != nullptr);
    NX_CHECK(snapshot->history().size() <= kMaxSnapshotRecordBytes / kSnapshotRecordBytes);
    bool gap_in_snapshot = false;
    bool only_engine = true;
    bool snapshot_has_error = false;
    for (std::size_t i = 0; i < snapshot->history().size(); ++i) {
        const auto record = snapshot->history().find(snapshot->history().seq_at(i));
        if (record.has_value() && !record->is_frame()) {
            gap_in_snapshot = true;
        } else if (record.has_value() && record->frame->app_id_text() != "Engine") {
            only_engine = false;
        }
        if (record.has_value() && record->is_frame() &&
            record->frame->log_level == core::LogLevel::Error) {
            snapshot_has_error = true;
        }
    }
    NX_CHECK(gap_in_snapshot);
    NX_CHECK(only_engine);
    NX_CHECK(snapshot_has_error);
    NX_CHECK(snapshot->find().count() > 0U);
    NX_CHECK(!snapshot->is_selected(old_seq));
    NX_CHECK(!snapshot->is_bookmarked(old_seq));
    NX_CHECK(!snapshot->time().anchor_ns.has_value() || snapshot->contains_stamp(1'000));
    NX_CHECK(snapshot->filter().criteria().text == "needle");
    NX_CHECK(!model->last_clone_status().empty());
    NX_CHECK(model->last_clone_status().find("selection") != std::string::npos);
    NX_CHECK(model->last_clone_status().find("bookmarks") != std::string::npos);
    NX_CHECK(model->last_clone_status().find("t0 cleared") != std::string::npos);

    const auto all_paused =
        model->clone_view(model->view_at(0)->id(), CloneMode::Paused, CloneOptions{});
    NX_CHECK(all_paused.has_value());
    if (all_paused.has_value()) {
        const LogView* all_snapshot = model->view(*all_paused);
        NX_CHECK(all_snapshot != nullptr);
        if (all_snapshot != nullptr) {
            bool has_engine = false;
            bool has_viz = false;
            for (std::size_t i = 0; i < all_snapshot->history().size(); ++i) {
                const auto record = all_snapshot->history().find(all_snapshot->history().seq_at(i));
                if (record.has_value() && record->is_frame()) {
                    has_engine = has_engine || record->frame->app_id_text() == "Engine";
                    has_viz = has_viz || record->frame->app_id_text() == "Viz";
                }
            }
            NX_CHECK(has_engine);
            NX_CHECK(has_viz);
            NX_CHECK(all_snapshot->history().size() <=
                     kMaxSnapshotRecordBytes / kSnapshotRecordBytes);
        }
    }

    // A paused clone of a paused view shares the immutable snapshot.
    const auto paused_of_paused = model->clone_view(*paused, CloneMode::Paused, CloneOptions{});
    NX_CHECK(paused_of_paused.has_value());
    if (paused_of_paused.has_value()) {
        LogView* shared = model->view(*paused_of_paused);
        NX_CHECK(shared != nullptr);
        NX_CHECK(shared->frozen() == snapshot->frozen());
        NX_CHECK(model->last_clone_status().empty());
    }

    // PAUSED limit is 16 independent views.
    std::size_t paused_count = model->paused_view_count();
    while (paused_count < kMaxPausedViews) {
        const auto extra = model->clone_view(*engine, CloneMode::Paused, CloneOptions{});
        NX_CHECK(extra.has_value());
        ++paused_count;
    }
    NX_CHECK(model->paused_view_count() == kMaxPausedViews);
    NX_CHECK(!model->clone_view(*engine, CloneMode::Paused, CloneOptions{}).has_value());
    const std::size_t snapshot_limit = kMaxSnapshotRecordBytes / kSnapshotRecordBytes;
    for (std::size_t i = 0; i < model->view_count(); ++i) {
        const LogView* view = model->view_at(i);
        if (view != nullptr && !view->live()) {
            NX_CHECK(view->history().size() <= snapshot_limit);
        }
    }

    // A PAUSED export keeps its snapshot alive after the view closes.
    LogView* export_view = model->view(*paused);
    if (export_view != nullptr && export_view->display_count() > 0U) {
        export_view->select_only(export_view->display_at(0));
        const std::string path = temp_path("paused-export.jsonl");
        NX_CHECK(model->start_export(*paused, path, ExportFormat::Jsonl).has_value());
        NX_CHECK(model->close_view(*paused).has_value());
        while (model->exporting()) {
            model->step_export(16U);
        }
        NX_CHECK(model->export_stats().written == 1U);
        static_cast<void>(std::remove(path.c_str()));
    }
}

void suite_view_config_defaults() {
    LogViewConfig config;
    config.views_saved = true;
    config.views.clear();
    auto empty_result = LogViewModel::create(64ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(empty_result.has_value());
    if (empty_result.has_value()) {
        NX_CHECK((*empty_result)->view_count() == 0U);
        NX_CHECK((*empty_result)->focused_view() == nullptr);
    }

    ViewConfig definition{};
    definition.id = 41U;
    definition.title = "All logs";
    definition.source = "";
    definition.text = "needle";
    definition.levels[static_cast<std::size_t>(core::LogLevel::Debug)] = false;
    definition.timestamp_mode = TimestampMode::SinceBoot;
    definition.delta_mode = DeltaMode::FromAnchor;
    definition.anomaly_threshold_ns = 12'345'678U;
    config.views.push_back(definition);
    auto restored_result = LogViewModel::create(64ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(restored_result.has_value());
    if (restored_result.has_value()) {
        LogViewModel& restored = **restored_result;
        NX_CHECK(restored.view_count() == 1U);
        const LogView* view = restored.view(41U);
        NX_CHECK(view != nullptr);
        if (view != nullptr) {
            NX_CHECK(view->id() == 41U);
            NX_CHECK(view->filter().criteria().text == "needle");
            NX_CHECK(
                !view->filter().criteria().levels[static_cast<std::size_t>(core::LogLevel::Debug)]);
            NX_CHECK(view->timestamp_mode() == TimestampMode::SinceBoot);
            NX_CHECK(view->delta_mode() == DeltaMode::FromAnchor);
            NX_CHECK(view->time().anomaly_threshold_ns == 12'345'678);
        }
    }
}

void suite_snapshot_budget() {
    LogViewConfig config;
    config.max_memory_mb = 64;
    auto model_result = LogViewModel::create(64ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(model_result.has_value());
    if (!model_result.has_value()) {
        return;
    }
    std::unique_ptr<LogViewModel> model = std::move(*model_result);
    const std::size_t limit = kMaxSnapshotRecordBytes / kSnapshotRecordBytes;
    for (std::uint64_t i = 0; i < limit + 200U; ++i) {
        model->ingest_frame(make_frame(1'000U + i, "Engine", core::LogLevel::Info, "snapshot"), 0U);
    }
    model->ingest_gap(RecordKind::TransportGap, 2U, 99'000U);
    const auto engine = model->create_view(ViewKind::Source, "Engine");
    NX_CHECK(engine.has_value());
    if (!engine.has_value()) {
        return;
    }
    const auto paused = model->clone_view(*engine, CloneMode::Paused, CloneOptions{});
    NX_CHECK(paused.has_value());
    if (!paused.has_value()) {
        return;
    }
    const LogView* snapshot = model->view(*paused);
    NX_CHECK(snapshot != nullptr);
    if (snapshot == nullptr) {
        return;
    }
    NX_CHECK(snapshot->history().size() <= limit);
    NX_CHECK(snapshot->history().size() > limit - 2U);
    NX_CHECK(snapshot->history().tail() == model->ring().tail());
    NX_CHECK(snapshot->history().head() > model->ring().head());
    NX_CHECK((snapshot->history().size() * kSnapshotRecordBytes) <= kMaxSnapshotRecordBytes);
}

void suite_all_reopen() {
    LogViewConfig config;
    config.max_memory_mb = 64;
    auto model_result = LogViewModel::create(64ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(model_result.has_value());
    if (!model_result.has_value()) {
        return;
    }
    std::unique_ptr<LogViewModel> model = std::move(*model_result);
    model->ingest_frame(make_frame(1'000, "Engine", core::LogLevel::Info, "first"), 0U);
    NX_CHECK(model->view_count() == 1U);
    LogView* all = model->view_at(0);
    NX_CHECK(all != nullptr);
    if (all == nullptr) {
        return;
    }
    const ViewId old_id = all->id();
    all->filter().criteria().apps.fill(false);
    all->filter().criteria().app_filter_active = true;
    NX_CHECK(model->close_view(old_id).has_value());
    NX_CHECK(model->live_view_count() == 0U);
    const auto reopened = model->create_view(ViewKind::All, {});
    NX_CHECK(reopened.has_value());
    if (reopened.has_value()) {
        LogView* view = model->view(*reopened);
        NX_CHECK(view != nullptr);
        if (view != nullptr) {
            NX_CHECK(view->kind() == ViewKind::All);
            NX_CHECK(!view->filter().criteria().app_filter_active);
            bool all_visible = true;
            for (const bool visible : view->filter().criteria().apps) {
                all_visible = all_visible && visible;
            }
            NX_CHECK(all_visible);
        }
    }
}

void suite_clone_options() {
    LogViewConfig config;
    config.max_memory_mb = 64;
    auto model_result = LogViewModel::create(64ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(model_result.has_value());
    if (!model_result.has_value()) {
        return;
    }
    std::unique_ptr<LogViewModel> model = std::move(*model_result);
    for (std::uint64_t i = 0; i < 30; ++i) {
        model->ingest_frame(make_frame(1'000U + i, "Engine", core::LogLevel::Info,
                                       i % 2 == 0U ? "needle" : "haystack"),
                            0U);
    }
    const auto source_id = model->create_view(ViewKind::Source, "Engine");
    NX_CHECK(source_id.has_value());
    if (!source_id.has_value()) {
        return;
    }
    LogView* source = model->view(*source_id);
    NX_CHECK(source != nullptr);
    if (source == nullptr) {
        return;
    }
    drain_view_rebuilds(*model);
    source->filter().criteria().text = "needle";
    source->filter().criteria().levels[static_cast<std::size_t>(core::LogLevel::Error)] = false;
    source->timestamp_mode() = TimestampMode::SinceBoot;
    source->delta_mode() = DeltaMode::FromAnchor;
    source->time().anomaly_threshold_ns = 123'000'000;
    NX_CHECK(source->display_count() > 0U);
    if (source->display_count() > 0U) {
        source->select_only(source->display_at(0));
    }
    NX_CHECK(source->toggle_bookmark(model->ring().head()));
    source->time().set_anchor(1'000);
    model->begin_find(*source, "needle");
    while (source->find().active()) {
        static_cast<void>(source->find().step(source->filter(), source->history(), 1'000U));
    }

    CloneOptions none{};
    none.levels = false;
    none.text = false;
    none.timestamp_mode = false;
    none.delta_mode = false;
    none.anomaly_threshold = false;
    none.selection = false;
    none.bookmarks = false;
    none.t0 = false;
    none.find = false;
    const auto clone_id = model->clone_view(*source_id, CloneMode::Live, none);
    NX_CHECK(clone_id.has_value());
    if (!clone_id.has_value()) {
        return;
    }
    const LogView* clone = model->view(*clone_id);
    NX_CHECK(clone != nullptr);
    if (clone == nullptr) {
        return;
    }
    NX_CHECK(clone->source() == source->source());
    NX_CHECK(clone->filter().criteria().text.empty());
    bool levels_default = true;
    for (const bool visible : clone->filter().criteria().levels) {
        levels_default = levels_default && visible;
    }
    NX_CHECK(levels_default);
    NX_CHECK(clone->timestamp_mode() == TimestampMode::Absolute);
    NX_CHECK(clone->delta_mode() == DeltaMode::PreviousVisible);
    NX_CHECK(clone->time().anomaly_threshold_ns ==
             static_cast<std::int64_t>(config.anomaly_threshold_ns));
    NX_CHECK(clone->selected_count() == 0U);
    NX_CHECK(clone->bookmarked_count() == 0U);
    NX_CHECK(!clone->time().anchor_ns.has_value());
    NX_CHECK(clone->find().query().empty());
    NX_CHECK(model->last_clone_status().empty());
}

void suite_navigation_queries() {
    LogViewConfig config;
    config.max_memory_mb = 64;
    auto model_result = LogViewModel::create(64ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(model_result.has_value());
    if (!model_result.has_value()) {
        return;
    }
    std::unique_ptr<LogViewModel> model = std::move(*model_result);
    for (std::uint64_t i = 0; i < 12; ++i) {
        model->ingest_frame(make_frame(1'000U + i, i % 2 == 0U ? "Engine" : "Viz",
                                       core::LogLevel::Info, i % 3 == 0U ? "needle" : "haystack"),
                            0U);
    }
    const auto source_id = model->create_view(ViewKind::Source, "Engine");
    NX_CHECK(source_id.has_value());
    if (!source_id.has_value()) {
        return;
    }
    drain_view_rebuilds(*model);
    LogView* source = model->view(*source_id);
    NX_CHECK(source != nullptr);
    if (source == nullptr) {
        return;
    }
    NX_CHECK(ui::first_visible_sequence(*source) == source->display_at(0));
    NX_CHECK(ui::last_visible_sequence(*source) ==
             source->display_at(source->display_count() - 1U));
    NX_CHECK(!ui::first_visible_sequence(*source).has_value() == (source->display_count() == 0U));
    NX_CHECK(ui::auto_scroll_active(true, true, false));
    NX_CHECK(!ui::auto_scroll_active(false, true, false));
    NX_CHECK(!ui::auto_scroll_active(true, true, true));
    NX_CHECK(!ui::auto_scroll_active(true, false, false));
    NX_CHECK(ui::manual_scroll_disables_auto_scroll(true, true));
    NX_CHECK(!ui::manual_scroll_disables_auto_scroll(true, false));
    NX_CHECK(!ui::manual_scroll_disables_auto_scroll(false, true));
    NX_CHECK(ui::shortcuts_allowed(true, false));
    NX_CHECK(!ui::shortcuts_allowed(true, true));
    NX_CHECK(!ui::shortcuts_allowed(false, false));

    source->set_paused(true);
    NX_CHECK(ui::first_visible_sequence(*source) == source->display_at(0));
    NX_CHECK(ui::last_visible_sequence(*source) ==
             source->display_at(source->display_count() - 1U));
    source->set_paused(false);

    source->filter().criteria().text = "needle";
    model->begin_filter_rebuild(*source);
    drain_view_rebuilds(*model);
    if (source->display_count() > 0U) {
        NX_CHECK(ui::first_visible_sequence(*source) == source->display_at(0));
        NX_CHECK(ui::last_visible_sequence(*source) ==
                 source->display_at(source->display_count() - 1U));
    }
    source->filter().criteria().text = "absent";
    model->begin_filter_rebuild(*source);
    drain_view_rebuilds(*model);
    NX_CHECK(!ui::first_visible_sequence(*source).has_value());
    NX_CHECK(!ui::last_visible_sequence(*source).has_value());

    const auto snapshot_id = model->clone_view(*source_id, CloneMode::Paused, CloneOptions{});
    NX_CHECK(snapshot_id.has_value());
    if (snapshot_id.has_value()) {
        LogView* snapshot = model->view(*snapshot_id);
        NX_CHECK(snapshot != nullptr);
        if (snapshot != nullptr) {
            if (snapshot->display_count() == 0U) {
                snapshot->filter().criteria().text.clear();
                snapshot->begin_rebuild();
                drain_view_rebuilds(*model);
            }
            if (snapshot->display_count() > 0U) {
                NX_CHECK(ui::first_visible_sequence(*snapshot) == snapshot->display_at(0));
                NX_CHECK(ui::last_visible_sequence(*snapshot) ==
                         snapshot->display_at(snapshot->display_count() - 1U));
            }
        }
    }
}

void suite_navigation_actions() {
    LogViewConfig config;
    config.max_memory_mb = 64;
    auto model_result = LogViewModel::create(64ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(model_result.has_value());
    if (!model_result.has_value()) {
        return;
    }
    std::unique_ptr<LogViewModel> model = std::move(*model_result);
    for (std::uint64_t i = 0; i < 12; ++i) {
        model->ingest_frame(make_frame(2'000U + i, i % 2 == 0U ? "Engine" : "Viz",
                                       core::LogLevel::Info, i % 3 == 0U ? "needle" : "haystack"),
                            0U);
    }
    NX_CHECK(model->view_count() > 0U);
    if (model->view_count() == 0U) {
        return;
    }
    drain_view_rebuilds(*model);
    LogView* view = model->view_at(0);
    NX_CHECK(view != nullptr);
    if (view == nullptr) {
        return;
    }
    NX_CHECK(view->display_count() > 1U);

    ui::ViewScrollState nav{};
    NX_CHECK(nav.auto_scroll);
    NX_CHECK(!nav.scroll_to.has_value());

    const std::uint64_t selected = view->display_at(1);
    view->select_only(selected);
    NX_CHECK(view->toggle_bookmark(selected));
    const auto record = view->history().find(selected);
    NX_CHECK(record.has_value());
    if (record.has_value()) {
        view->time().set_anchor(static_cast<std::int64_t>(record->frame->timestamp_ns));
    }
    const std::uint64_t bookmarks_before = view->bookmarked_count();
    const auto selected_before = view->last_selected();
    const auto anchor_before = view->time().anchor_ns;
    const auto levels_before = view->filter().criteria().levels;
    view->set_paused(true);

    ui::navigate_first(*view, nav);
    NX_CHECK(nav.scroll_to == ui::first_visible_sequence(*view));
    NX_CHECK(!nav.auto_scroll);
    NX_CHECK(view->last_selected() == selected_before);
    NX_CHECK(view->bookmarked_count() == bookmarks_before);
    NX_CHECK(view->time().anchor_ns == anchor_before);
    NX_CHECK(view->paused());
    NX_CHECK(view->filter().criteria().levels == levels_before);

    nav.scroll_to.reset();
    ui::navigate_last(*view, nav);
    NX_CHECK(nav.scroll_to == ui::last_visible_sequence(*view));
    NX_CHECK(!nav.auto_scroll);

    nav.auto_scroll = true;
    nav.scroll_to.reset();
    ui::navigate_last(*view, nav);
    NX_CHECK(nav.scroll_to == ui::last_visible_sequence(*view));
    NX_CHECK(nav.auto_scroll);

    view->set_paused(false);

    ui::toggle_auto_scroll(*view, nav);
    NX_CHECK(!nav.auto_scroll);
    ui::toggle_auto_scroll(*view, nav);
    NX_CHECK(nav.auto_scroll);
    NX_CHECK(nav.scroll_to == ui::last_visible_sequence(*view));

    view->filter().criteria().text = "needle";
    model->begin_filter_rebuild(*view);
    drain_view_rebuilds(*model);
    NX_CHECK(view->display_count() > 0U);
    NX_CHECK(view->display_count() < 12U);
    nav.scroll_to.reset();
    ui::navigate_first(*view, nav);
    NX_CHECK(nav.scroll_to == ui::first_visible_sequence(*view));
    NX_CHECK(nav.scroll_to == view->display_at(0));

    view->filter().criteria().text = "absent";
    model->begin_filter_rebuild(*view);
    drain_view_rebuilds(*model);
    NX_CHECK(view->display_count() == 0U);
    ui::ViewScrollState empty{};
    empty.auto_scroll = false;
    ui::navigate_first(*view, empty);
    NX_CHECK(!empty.scroll_to.has_value());
    NX_CHECK(!empty.auto_scroll);
    ui::navigate_last(*view, empty);
    NX_CHECK(!empty.scroll_to.has_value());
    NX_CHECK(!empty.auto_scroll);
}

} // namespace

int main() {
    suite_cli_options();
    suite_vocabulary();
    suite_app_registry();
    suite_record_ring();
    suite_filter();
    suite_find();
    suite_time();
    suite_time_base();
    suite_colors();
    suite_minimap();
    suite_export_import();
    suite_config();
    suite_resource_guard();
    suite_model();
    suite_views();
    suite_paused_views();
    suite_view_config_defaults();
    suite_snapshot_budget();
    suite_all_reopen();
    suite_clone_options();
    suite_navigation_queries();
    suite_navigation_actions();
    suite_scale();
    suite_allocation();
    suite_memory_budget();
    suite_import_session();
    suite_thread_spec();
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
