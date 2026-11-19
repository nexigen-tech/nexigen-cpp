// nexigen::chrono — time-base daemon configuration serialization and loader
// (change add-time-base-service; spec chrono/time-base "Daemon configuration").
//
// Everything here is cold path: file I/O, nlohmann::json and std::string are
// explicitly allowed at daemon start and never reach the publish loop.
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/settings_io.hpp>
#include <nexigen/chrono/time_base_daemon_config.hpp>

namespace nexigen::chrono {
namespace {

/// @brief Read a non-negative duration field in nanoseconds.
///
/// @param document JSON object to probe.
/// @param key Field name.
/// @param out Destination duration, untouched when absent or invalid.
/// @return `true` when the field was present and non-negative.
bool read_duration(const nlohmann::json& document, const char* key,
                   std::chrono::nanoseconds& out) noexcept {
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_integer()) {
        return true;
    }
    const auto value = it->get<std::int64_t>();
    if (value < 0) {
        return false;
    }
    out = std::chrono::nanoseconds{value};
    return true;
}

/// @brief Reject malformed or negative duration fields before field-wise reads.
///
/// @param document Parsed configuration object.
/// @return `true` when every recognized duration is a non-negative integer.
[[nodiscard]] bool valid_duration_fields(const nlohmann::json& document) {
    for (const char* key : {"publish_interval_ns", "fit_window_ns", "holdover_window_ns"}) {
        const auto it = document.find(key);
        if (it != document.end() && (!it->is_number_integer() || it->get<std::int64_t>() < 0)) {
            return false;
        }
    }
    return true;
}

/// @brief Validate a fully parsed configuration.
///
/// @param config Candidate configuration.
/// @return `true` when every duration, threshold, thread spec and domain name is usable.
[[nodiscard]] bool valid_config(const TimeBaseDaemonConfig& config) {
    if (config.publish_interval <= std::chrono::nanoseconds{0} ||
        config.fit_window <= std::chrono::nanoseconds{0} ||
        config.holdover_window < std::chrono::nanoseconds{0}) {
        return false;
    }
    if (config.time_sync.max_step_ns < 0 || config.time_sync.max_rate_ppm < 0 ||
        config.time_sync.min_window_ns < 0) {
        return false;
    }
    if (!config.main_thread.validate().has_value()) {
        return false;
    }
    for (const auto& domain : config.domains) {
        if (domain.name.empty()) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string HardwareDomainConfig::to_string() const {
    return std::format(
        "HardwareDomainConfig(name={}, uuid={}, access={}, mode={}, simulate={}, nominal_hz={})",
        name, uuid, access, mode, simulate, nominal_hz);
}

std::string to_string(const HardwareDomainConfig& domain) {
    return domain.to_string();
}

void to_json(nlohmann::json& j, const HardwareDomainConfig& domain) {
    j = nlohmann::json{
        {"name", domain.name}, {"uuid", domain.uuid},         {"access", domain.access},
        {"mode", domain.mode}, {"simulate", domain.simulate}, {"nominal_hz", domain.nominal_hz},
    };
}

void from_json(const nlohmann::json& j, HardwareDomainConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_string(j, "name", out.name);
    core::detail::read_string(j, "uuid", out.uuid);
    core::detail::read_string(j, "access", out.access);
    if (const auto mode_it = j.find("mode"); mode_it != j.end()) {
        from_json(*mode_it, out.mode);
    }
    core::detail::read_bool(j, "simulate", out.simulate);
    std::uint64_t nominal_hz = out.nominal_hz;
    if (core::detail::read_counter(j, "nominal_hz", nominal_hz)) {
        out.nominal_hz = nominal_hz;
    }
}

std::string TimeBaseDaemonConfig::to_string() const {
    return std::format(
        "TimeBaseDaemonConfig(segment_name={}, publish_interval_ns={}, fit_window_ns={}, "
        "max_residual_ns={}, publish_tsc={}, sync_mode={}, holdover_window_ns={}, "
        "history_path={}, record_session_path={}, domains={})",
        segment_name, publish_interval.count(), fit_window.count(), max_residual_ns, publish_tsc,
        sync_mode, holdover_window.count(), history_path, record_session_path, domains.size());
}

std::string to_string(const TimeBaseDaemonConfig& config) {
    return config.to_string();
}

void to_json(nlohmann::json& j, const TimeBaseDaemonConfig& config) {
    j = nlohmann::json{
        {"segment_name", config.segment_name},
        {"publish_interval_ns", config.publish_interval.count()},
        {"fit_window_ns", config.fit_window.count()},
        {"max_residual_ns", config.max_residual_ns},
        {"publish_tsc", config.publish_tsc},
        {"sync_mode", config.sync_mode},
        {"time_sync",
         {{"max_step_ns", config.time_sync.max_step_ns},
          {"max_rate_ppm", config.time_sync.max_rate_ppm},
          {"min_window_ns", config.time_sync.min_window_ns}}},
        {"holdover_window_ns", config.holdover_window.count()},
        {"history_path", config.history_path},
        {"record_session_path", config.record_session_path},
        {"domains", config.domains},
        {"main_thread", config.main_thread},
    };
}

void from_json(const nlohmann::json& j, TimeBaseDaemonConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_string(j, "segment_name", out.segment_name);
    static_cast<void>(read_duration(j, "publish_interval_ns", out.publish_interval));
    static_cast<void>(read_duration(j, "fit_window_ns", out.fit_window));
    std::uint64_t max_residual = out.max_residual_ns;
    if (core::detail::read_counter(j, "max_residual_ns", max_residual)) {
        out.max_residual_ns = max_residual;
    }
    core::detail::read_bool(j, "publish_tsc", out.publish_tsc);
    if (const auto mode_it = j.find("sync_mode"); mode_it != j.end()) {
        from_json(*mode_it, out.sync_mode);
    }
    if (const auto policy_it = j.find("time_sync");
        policy_it != j.end() && policy_it->is_object()) {
        const auto& policy = *policy_it;
        const auto read_int = [&policy](const char* key, std::int64_t& out_value) {
            const auto it = policy.find(key);
            if (it != policy.end() && it->is_number_integer()) {
                out_value = it->get<std::int64_t>();
            }
        };
        read_int("max_step_ns", out.time_sync.max_step_ns);
        read_int("max_rate_ppm", out.time_sync.max_rate_ppm);
        read_int("min_window_ns", out.time_sync.min_window_ns);
    }
    static_cast<void>(read_duration(j, "holdover_window_ns", out.holdover_window));
    core::detail::read_string(j, "record_session_path", out.record_session_path);
    core::detail::read_string(j, "history_path", out.history_path);
    if (const auto thread_it = j.find("main_thread"); thread_it != j.end()) {
        core::from_json(*thread_it, out.main_thread);
    }
    const auto domains_it = j.find("domains");
    if (domains_it != j.end() && domains_it->is_array()) {
        out.domains.clear();
        for (const auto& item : *domains_it) {
            HardwareDomainConfig domain{};
            from_json(item, domain);
            out.domains.push_back(std::move(domain));
        }
    }
}

core::Status load_time_base_daemon_config(std::string_view path,
                                          TimeBaseDaemonConfig& out) noexcept {
    if (path.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    std::string text;
    const core::settings_io::FileRead read = core::settings_io::read_text_file(path, text);
    if (read == core::settings_io::FileRead::Absent) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    if (read == core::settings_io::FileRead::Failed) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    // Reject invalid duration types before they can be silently ignored.
    if (!valid_duration_fields(document)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    TimeBaseDaemonConfig candidate{};
    from_json(document, candidate);
    if (!valid_config(candidate)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    out = std::move(candidate);
    return {};
}

} // namespace nexigen::chrono
