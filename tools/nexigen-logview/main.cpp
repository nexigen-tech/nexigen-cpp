// nexigen-logview — realtime LogFrame viewer (change add-realtime-log-viewer).
//
// Attaches to the platform log service, keeps a bounded history in a single
// mmap, and renders it with Dear ImGui. The headless path exercises the same
// model without OpenGL.
#include <cstdio>
#include <string>

#include <nexigen/chrono/time_base_clock.hpp>
#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/core/result.hpp>

#include "cli.hpp"
#include "headless.hpp"
#include "importer.hpp"
#include "model/log_view_model.hpp"

#if defined(NEXIGEN_LOGVIEW_GUI)
#include "ui/logview_window.hpp"
#endif

namespace {

namespace chrono = nexigen::chrono;
namespace logview = nexigen::logview;

std::string resolve_config_path(const logview::CliOptions& options) {
    return options.config_given ? options.config_path : logview::default_config_path();
}

} // namespace

int main(int argc, char* argv[]) {
    const auto parsed = logview::parse_cli(argc, argv);
    if (!parsed) {
        std::fputs(logview::usage_text().data(), stderr);
        return 2;
    }
    const logview::CliOptions options = *parsed;
    if (options.help) {
        std::fputs(logview::usage_text().data(), stdout);
        return 0;
    }

    logview::LogViewConfig config{};
    bool warning = false;
    const std::string config_path = resolve_config_path(options);
    const auto loaded = logview::load_config(config_path, config, warning);
    if (warning) {
        std::fprintf(stderr, "nexigen-logview: ignoring corrupt configuration %s\n",
                     config_path.c_str());
    }
    if (!loaded) {
        std::fprintf(stderr, "nexigen-logview: cannot read configuration %s\n",
                     config_path.c_str());
    }
    if (options.service_given) {
        config.service_name = options.service_name;
    }
    if (options.memory_given) {
        config.max_memory_mb = options.memory_mb;
    }
    const auto valid = config.validate();
    if (!valid) {
        std::fprintf(stderr, "nexigen-logview: invalid configuration\n");
        return 1;
    }

    std::int64_t utc_offset_ns = 0;
    static_cast<void>(chrono::sample_utc_offset(utc_offset_ns));
    static_cast<void>(chrono::TimeBaseClock::attach("/nexigen-timebase"));

    const std::uint64_t budget =
        static_cast<std::uint64_t>(config.max_memory_mb) * 1024ULL * 1024ULL;
    auto model_result = logview::LogViewModel::create(budget, config);
    if (!model_result) {
        std::fprintf(stderr, "nexigen-logview: cannot allocate the %u MiB history\n",
                     config.max_memory_mb);
        return 1;
    }
    std::unique_ptr<logview::LogViewModel> model = std::move(*model_result);
    model->base_time().utc_offset_ns = utc_offset_ns;
    model->base_time().time_base_attached = chrono::TimeBaseClock::attached();

    if (options.headless) {
        return logview::run_headless(*model, options);
    }

    if (options.offline) {
        logview::ImportStats stats{};
        const auto imported = model->import_now(options.offline_file, stats);
        if (!imported) {
            std::fprintf(stderr, "nexigen-logview: cannot open %s\n", options.offline_file.c_str());
            return 1;
        }
        for (;;) {
            model->step_filter_rebuilds(20'000U);
            bool rebuilding = false;
            for (std::size_t i = 0; i < model->view_count(); ++i) {
                const logview::LogView* view = model->view_at(i);
                rebuilding = rebuilding || (view != nullptr && view->rebuilding());
            }
            if (!rebuilding) {
                break;
            }
        }
    }

#if defined(NEXIGEN_LOGVIEW_GUI)
    logview::UiOptions ui{};
    ui.live = !options.offline;
    ui.smoke = options.ui_smoke;
    ui.actions_test = options.ui_actions_test;
    ui.input_test = options.ui_input_test;
    ui.view_test = options.ui_view_test;
    ui.focus_test = options.ui_focus_test;
    ui.ui_scale = options.ui_scale;
    ui.open_path = options.offline_file;
    const int result = logview::run_ui(*model, ui);
    logview::LogViewConfig persisted = model->config();
    persisted.colors = model->colors();
    persisted.level_visible = model->filter().criteria().levels;
    persisted.text_filter = model->filter().criteria().text;
    persisted.anomaly_threshold_ns = static_cast<std::uint64_t>(model->time().anomaly_threshold_ns);
    persisted.views = model->view_configs();
    persisted.views_saved = true;
    persisted.hidden_apps.clear();
    for (std::size_t slot = 0; slot < logview::AppRegistry::capacity; ++slot) {
        const logview::AppSlot& app = model->ring().apps().at(slot);
        if (app.used && !model->filter().criteria().apps[slot]) {
            persisted.hidden_apps.emplace_back(app.text());
        }
    }
    static_cast<void>(logview::save_config(config_path, persisted));
    return result;
#else
    std::fprintf(stderr, "nexigen-logview: built without GUI support\n");
    return 1;
#endif
}
