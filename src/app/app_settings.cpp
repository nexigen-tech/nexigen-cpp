// nexigen::app — cold-path settings aggregation (change
// refactor-settings-into-subsystem-modules; design D4).
//
// The app layer no longer owns any subsystem's configuration semantics: it
// aggregates the four module `Settings` values and delegates each load to the
// owning module's loader. Everything here runs during initialization:
// std::string and nlohmann::json are allowed; no producer or tick path calls
// into this unit. nlohmann/json headers trigger -Wnull-dereference under -O3
// (pre-existing upstream issue), so the pragma guards this cold unit only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <nexigen/app/app_settings.hpp>

namespace nexigen::app {

std::string AppSettingsPaths::to_string() const {
    return "AppSettingsPaths(logging=" + logging.to_string() +
           ", metrics=" + metrics.to_string() + ", tracing=" + tracing.to_string() +
           ", chrono=" + chrono.to_string() + ")";
}

std::string Settings::to_string() const {
    return "Settings(app_name=\"" + app_name +
           "\", logging=" + logging.to_string() +
           ", metrics=" + metrics.to_string() + ", tracing=" + tracing.to_string() +
           ", chrono=" + chrono.to_string() + ")";
}

void to_json(nlohmann::json& j, const AppSettingsPaths& paths) {
    j = nlohmann::json{{"logging", nlohmann::json{{"global_file", paths.logging.global_file},
                                                  {"local_file", paths.logging.local_file}}},
                       {"metrics", nlohmann::json{{"global_file", paths.metrics.global_file},
                                                  {"local_file", paths.metrics.local_file}}},
                       {"tracing", nlohmann::json{{"global_file", paths.tracing.global_file},
                                                  {"local_file", paths.tracing.local_file}}},
                       {"chrono", nlohmann::json{{"global_file", paths.chrono.global_file},
                                                 {"local_file", paths.chrono.local_file}}}};
}

void to_json(nlohmann::json& j, const Settings& settings) {
    nlohmann::json logging_json;
    to_json(logging_json, settings.logging);
    nlohmann::json metrics_json;
    to_json(metrics_json, settings.metrics);
    nlohmann::json tracing_json;
    to_json(tracing_json, settings.tracing);
    nlohmann::json chrono_json;
    to_json(chrono_json, settings.chrono);
    j = nlohmann::json{{"app_name", settings.app_name},
                       {"logging", std::move(logging_json)},
                       {"metrics", std::move(metrics_json)},
                       {"tracing", std::move(tracing_json)},
                       {"chrono", std::move(chrono_json)}};
}

core::Status load_app_settings(const AppSettingsPaths& paths,
                               Settings& out) noexcept {
    out = Settings{};
    const core::Status logging_status =
        logging::load_logging_settings(paths.logging, out.logging);
    const core::Status metrics_status =
        metrics::load_metrics_settings(paths.metrics, out.metrics);
    const core::Status tracing_status =
        tracing::load_tracing_settings(paths.tracing, out.tracing);
    const core::Status chrono_status =
        chrono::load_chrono_settings(paths.chrono, out.chrono);

    if (!logging_status || !metrics_status || !tracing_status || !chrono_status) {
        if ((!logging_status && logging_status.error() == core::ErrorCode::SystemError) ||
            (!metrics_status && metrics_status.error() == core::ErrorCode::SystemError) ||
            (!tracing_status && tracing_status.error() == core::ErrorCode::SystemError) ||
            (!chrono_status && chrono_status.error() == core::ErrorCode::SystemError)) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

} // namespace nexigen::app
