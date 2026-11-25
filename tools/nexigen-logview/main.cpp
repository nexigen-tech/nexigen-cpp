// nexigen-logview — realtime LogFrame viewer (change add-realtime-log-viewer).
//
// Attaches to the platform log service, keeps a bounded history in a single
// mmap, and renders it with Dear ImGui. The headless path exercises the same
// model without OpenGL.
#include <cstdio>
#include <memory>
#include <optional>
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

/// @brief Parse the command line, handling `--help` and usage errors.
///
/// @param argc Argument count.
/// @param argv Argument vector.
/// @param exit_code Receives the exit code when parsing ends the process.
/// @return The options, or `std::nullopt` when the process should exit now.
std::optional<logview::CliOptions> parse_options(int argc, char* argv[], int& exit_code) {
    const auto parsed = logview::parse_cli(argc, argv);
    if (!parsed) {
        std::fputs(logview::usage_text().data(), stderr);
        exit_code = 2;
        return std::nullopt;
    }
    if (parsed->help) {
        std::fputs(logview::usage_text().data(), stdout);
        exit_code = 0;
        return std::nullopt;
    }
    return *parsed;
}

/// @brief Load, override and validate the effective configuration.
///
/// @param options Parsed command line.
/// @param config_path Receives the path used for loading and later persistence.
/// @return The validated configuration, or `std::nullopt` when it is invalid.
std::optional<logview::LogViewConfig> resolve_config(const logview::CliOptions& options,
                                                     std::string& config_path) {
    logview::LogViewConfig config{};
    bool warning = false;
    config_path = resolve_config_path(options);
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
    if (!config.validate()) {
        std::fprintf(stderr, "nexigen-logview: invalid configuration\n");
        return std::nullopt;
    }
    return config;
}

/// @brief Attach the time base and allocate the history model.
///
/// @param config Validated configuration.
/// @return The model, or `nullptr` when the history cannot be allocated.
std::unique_ptr<logview::LogViewModel> create_model(const logview::LogViewConfig& config) {
    std::int64_t utc_offset_ns = 0;
    static_cast<void>(chrono::sample_utc_offset(utc_offset_ns));
    static_cast<void>(chrono::TimeBaseClock::attach("/nexigen-timebase"));

    const std::uint64_t budget =
        static_cast<std::uint64_t>(config.max_memory_mb) * 1024ULL * 1024ULL;
    auto model_result = logview::LogViewModel::create(budget, config);
    if (!model_result) {
        std::fprintf(stderr, "nexigen-logview: cannot allocate the %u MiB history\n",
                     config.max_memory_mb);
        return nullptr;
    }
    std::unique_ptr<logview::LogViewModel> model = std::move(*model_result);
    model->base_time().utc_offset_ns = utc_offset_ns;
    model->base_time().time_base_attached = chrono::TimeBaseClock::attached();
    return model;
}

/// @brief Import an offline file and drain the filter rebuilds.
///
/// @param model View model receiving the import.
/// @param options Parsed command line naming the file.
/// @return `false` when the file cannot be opened.
bool import_offline(logview::LogViewModel& model, const logview::CliOptions& options) {
    logview::ImportStats stats{};
    const auto imported = model.import_now(options.offline_file, stats);
    if (!imported) {
        std::fprintf(stderr, "nexigen-logview: cannot open %s\n", options.offline_file.c_str());
        return false;
    }
    for (;;) {
        model.step_filter_rebuilds(20'000U);
        bool rebuilding = false;
        for (std::size_t i = 0; i < model.view_count(); ++i) {
            const logview::LogView* view = model.view_at(i);
            rebuilding = rebuilding || (view != nullptr && view->rebuilding());
        }
        if (!rebuilding) {
            break;
        }
    }
    return true;
}

#if defined(NEXIGEN_LOGVIEW_GUI)
/// @brief Build the UI options from the parsed command line.
///
/// @param options Parsed command line.
/// @return UI options for `run_ui`.
logview::UiOptions make_ui_options(const logview::CliOptions& options) {
    logview::UiOptions ui{};
    ui.live = !options.offline;
    ui.smoke = options.ui_smoke;
    ui.actions_test = options.ui_actions_test;
    ui.input_test = options.ui_input_test;
    ui.view_test = options.ui_view_test;
    ui.focus_test = options.ui_focus_test;
    ui.splash_test = options.ui_splash_test;
    ui.ui_scale = options.ui_scale;
    ui.open_path = options.offline_file;
    return ui;
}

/// @brief Persist the model's view and filter state to the configuration file.
///
/// @param model View model supplying the current state.
/// @param config_path Destination path.
void persist_config(logview::LogViewModel& model, const std::string& config_path) {
    logview::LogViewConfig persisted = model.config();
    persisted.colors = model.colors();
    persisted.level_visible = model.filter().criteria().levels;
    persisted.text_filter = model.filter().criteria().text;
    persisted.anomaly_threshold_ns = static_cast<std::uint64_t>(model.time().anomaly_threshold_ns);
    persisted.views = model.view_configs();
    persisted.views_saved = true;
    persisted.hidden_apps.clear();
    for (std::size_t slot = 0; slot < logview::AppRegistry::capacity; ++slot) {
        const logview::AppSlot& app = model.ring().apps().at(slot);
        if (app.used && !model.filter().criteria().apps[slot]) {
            persisted.hidden_apps.emplace_back(app.text());
        }
    }
    static_cast<void>(logview::save_config(config_path, persisted));
}
#endif

/// @brief Run the GUI session and persist the resulting state.
///
/// @param model View model driving the session.
/// @param options Parsed command line.
/// @param config_path Destination path for the persisted state.
/// @return Process exit code.
int run_gui(logview::LogViewModel& model, const logview::CliOptions& options,
            const std::string& config_path) {
#if defined(NEXIGEN_LOGVIEW_GUI)
    const int result = logview::run_ui(model, make_ui_options(options));
    persist_config(model, config_path);
    return result;
#else
    static_cast<void>(model);
    static_cast<void>(options);
    static_cast<void>(config_path);
    std::fprintf(stderr, "nexigen-logview: built without GUI support\n");
    return 1;
#endif
}

} // namespace

int main(int argc, char* argv[]) {
    int exit_code = 0;
    const auto options = parse_options(argc, argv, exit_code);
    if (!options.has_value()) {
        return exit_code;
    }
    std::string config_path;
    const auto config = resolve_config(*options, config_path);
    if (!config.has_value()) {
        return 1;
    }
    auto model = create_model(*config);
    if (!model) {
        return 1;
    }
    if (options->headless) {
        return logview::run_headless(*model, *options);
    }
    if (options->offline && !import_offline(*model, *options)) {
        return 1;
    }
    return run_gui(*model, *options, config_path);
}
