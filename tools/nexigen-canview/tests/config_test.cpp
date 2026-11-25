// CLI, configuration and status snapshot tests.
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "can_record_ring.hpp"
#include "canview_config.hpp"
#include "cli.hpp"
#include "eds_library.hpp"
#include "overwrite_table.hpp"
#include "session_import.hpp"
#include "status_snapshot.hpp"
#include "suite.hpp"
#include "support/check.hpp"

namespace {

namespace canview = nexigen::canview;

canview::CanViewChannel make_channel(std::string name, std::uint16_t id, bool enabled = true) {
    canview::CanViewChannel channel{};
    channel.name = std::move(name);
    channel.channel_id = id;
    channel.enabled = enabled;
    return channel;
}

void test_cli_parser() {
    const char* argv[] = {"canview",
                          "--channel",
                          "powertrain:1",
                          "--eds",
                          "1:2:/tmp/drive.eds",
                          "--views",
                          "2",
                          "--memory-mb",
                          "64",
                          "--shared-timebase",
                          "--time-columns",
                          "both",
                          "--timestamp-mode",
                          "since-session",
                          "--delta-mode",
                          "per-id",
                          "--filter-id",
                          "0x180",
                          "--filter-mask",
                          "0x780",
                          "--filter-text",
                          "abc",
                          "--errors-only",
                          "--show-bus-idle"};
    auto parsed = canview::parse_cli(static_cast<int>(std::size(argv)), const_cast<char**>(argv));
    NX_CHECK(parsed.has_value());
    if (!parsed) {
        return;
    }
    NX_CHECK(parsed->channels.size() == 1U);
    if (!parsed->channels.empty()) {
        NX_CHECK(parsed->channels[0].name == "powertrain");
        NX_CHECK(parsed->channels[0].channel_id == 1U);
        NX_CHECK(parsed->channels[0].enabled);
    }
    NX_CHECK(parsed->eds.size() == 1U);
    if (!parsed->eds.empty()) {
        NX_CHECK(parsed->eds[0].channel_id == 1U);
        NX_CHECK(parsed->eds[0].node_id == 2U);
        NX_CHECK(parsed->eds[0].path == "/tmp/drive.eds");
    }
    NX_CHECK(parsed->view_count == 2U);
    NX_CHECK(parsed->memory_mb == 64U);
    NX_CHECK(parsed->shared_timebase);
    NX_CHECK(parsed->time_columns == canview::TimeColumnMode::Both);
    NX_CHECK(parsed->timestamp_mode == canview::TimestampMode::SinceSessionStart);
    NX_CHECK(parsed->delta_mode == canview::DeltaMode::PerCanId);
    NX_CHECK(parsed->filter_id_given);
    NX_CHECK(parsed->filter_id == 0x180U);
    NX_CHECK(parsed->filter_mask == 0x780U);
    NX_CHECK(parsed->filter_text == "abc");
    NX_CHECK(parsed->errors_only);
    NX_CHECK(parsed->show_bus_idle);

    const char* headless[] = {"canview",    "--offline",  "/tmp/s.jsonl",
                              "--headless", "--json",     "--max-records",
                              "5",          "--mock",     "10",
                              "--soak",     "1",          "--rate",
                              "1000",       "--ui-scale", "2"};
    parsed =
        canview::parse_cli(static_cast<int>(std::size(headless)), const_cast<char**>(headless));
    NX_CHECK(parsed.has_value());
    if (parsed) {
        NX_CHECK(parsed->offline);
        NX_CHECK(parsed->headless);
        NX_CHECK(parsed->json);
        NX_CHECK(parsed->max_records == 5U);
        NX_CHECK(parsed->mock_records == 10U);
        NX_CHECK(parsed->soak_seconds == 1U);
        NX_CHECK(parsed->mock_rate == 1000U);
        NX_CHECK(parsed->ui_scale == 2.0F);
    }

    const char* bad[] = {"canview", "--memory-mb", "8"};
    NX_CHECK(!canview::parse_cli(2, const_cast<char**>(bad)).has_value());
    const char* bad_views[] = {"canview", "--views", "9"};
    NX_CHECK(!canview::parse_cli(3, const_cast<char**>(bad_views)).has_value());
    const char* bad_rate[] = {"canview", "--rate", "0"};
    NX_CHECK(!canview::parse_cli(3, const_cast<char**>(bad_rate)).has_value());
    const char* bad_channel[] = {"canview", "--channel", ":1"};
    NX_CHECK(!canview::parse_cli(3, const_cast<char**>(bad_channel)).has_value());
    const char* bad_option[] = {"canview", "--nope"};
    NX_CHECK(!canview::parse_cli(2, const_cast<char**>(bad_option)).has_value());
}

void test_config_roundtrip() {
    canview::CanViewConfig config{};
    config.channels.push_back(make_channel("powertrain", 1U));
    config.channels.push_back(make_channel("chassis", 2U, false));
    config.memory_mb = 384U;
    config.view_count = 2U;
    config.shared_timebase = true;
    config.filter.set_text("needle");
    config.views =
        nlohmann::json{{"views", nlohmann::json::array({{{"kind", "All"}, {"title", "Saved"}}})}};
    config.defaults.time_columns = canview::TimeColumnMode::Both;
    NX_CHECK(config.validate().has_value());

    const std::string path = "/tmp/opencode/canview-config-test.json";
    NX_CHECK(canview::save_config(path, config).has_value());
    bool corrupted = false;
    const auto loaded = canview::load_config(path, &corrupted);
    NX_CHECK(!corrupted);
    NX_CHECK(loaded.channels.size() == 2U);
    NX_CHECK(loaded.memory_mb == 384U);
    NX_CHECK(loaded.view_count == 2U);
    NX_CHECK(loaded.shared_timebase);
    NX_CHECK(loaded.filter.text == "needle");
    NX_CHECK(loaded.defaults.time_columns == canview::TimeColumnMode::Both);
    NX_CHECK(loaded.views.is_object());
    NX_CHECK(loaded.views["views"].size() == 1U);
    NX_CHECK(!std::ifstream(path + ".tmp").good());

    const nlohmann::json with_unknown = nlohmann::json{
        {"memory_mb", 128},
        {"unknown_field", 42},
        {"channels",
         nlohmann::json::array({{{"name", "one"}, {"channel_id", 1}, {"enabled", true}}})}};
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << with_unknown.dump();
    }
    const auto tolerant = canview::load_config(path, &corrupted);
    NX_CHECK(!corrupted);
    NX_CHECK(tolerant.memory_mb == 128U);
    NX_CHECK(tolerant.channels.size() == 1U);

    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "{not json";
    }
    {
        std::ifstream before(path, std::ios::binary);
        const std::string original((std::istreambuf_iterator<char>(before)),
                                   std::istreambuf_iterator<char>());
        const auto broken = canview::load_config(path, &corrupted);
        NX_CHECK(corrupted);
        NX_CHECK(broken.memory_mb == 256U);
        std::ifstream after(path, std::ios::binary);
        const std::string unchanged((std::istreambuf_iterator<char>(after)),
                                    std::istreambuf_iterator<char>());
        NX_CHECK(original == unchanged);
    }

    static_cast<void>(::setenv("XDG_CONFIG_HOME", "/tmp/opencode/xdg-config", 1));
    NX_CHECK(canview::default_config_path() == "/tmp/opencode/xdg-config/nexigen/canview.json");
    static_cast<void>(::unsetenv("XDG_CONFIG_HOME"));

    canview::CanViewConfig too_many{};
    for (std::uint16_t index = 0U; index < 9U; ++index) {
        too_many.channels.push_back(make_channel("ch" + std::to_string(index), index));
    }
    NX_CHECK(!too_many.validate().has_value());
    canview::CanViewConfig too_many_profiles{};
    for (std::uint16_t index = 0U; index < 33U; ++index) {
        too_many_profiles.channels.push_back(
            make_channel("ch" + std::to_string(index), index, index < 8U));
    }
    NX_CHECK(!too_many_profiles.validate().has_value());
    canview::CanViewConfig duplicate{};
    duplicate.channels.push_back(make_channel("dup", 1U));
    duplicate.channels.push_back(make_channel("dup", 2U));
    NX_CHECK(!duplicate.validate().has_value());

    canview::CliOptions options{};
    options.channels = {make_channel("override", 9U)};
    options.memory_given = true;
    options.memory_mb = 64U;
    options.filter_text = "cli";
    options.filter_id = 0x180U;
    options.filter_id_given = true;
    options.filter_mask = 0x780U;
    options.errors_only = true;
    canview::apply_cli_overrides(config, options);
    NX_CHECK(config.memory_mb == 64U);
    NX_CHECK(config.channels.size() == 1U);
    NX_CHECK(config.channels[0].name == "override");
    NX_CHECK(config.filter.text == "cli");
    NX_CHECK(config.filter.id_filter_active);
    NX_CHECK(config.filter.errors_only);
}

void test_status_snapshot() {
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring) {
        return;
    }
    canview::OverwriteTable overwrite;
    overwrite.init(16U);
    canview::EdsLibrary eds;
    canview::CanOpenReindexer reindex;
    canview::CobIdRouter router;
    reindex.begin(*ring, router);
    canview::ImportResult imported{};
    imported.records.resize(3U);
    imported.malformed = 1U;

    canview::StatusSources sources{};
    sources.ring = &*ring;
    sources.overwrite = &overwrite;
    sources.eds = &eds;
    sources.reindex = &reindex;
    sources.import = &imported;
    const auto status = canview::collect_status(sources);
    NX_CHECK(status.history_capacity == 1024U);
    NX_CHECK(status.memory_bytes > 0U);
    NX_CHECK(status.import_records == 3U);
    NX_CHECK(status.import_malformed == 1U);
    NX_CHECK(!status.to_string().empty());
}

} // namespace

namespace canview_test {

void run_config_tests() {
    test_cli_parser();
    test_config_roundtrip();
    test_status_snapshot();
}

} // namespace canview_test
