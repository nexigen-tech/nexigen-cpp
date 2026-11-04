// nexigen::logging — global/local logging settings loader (change
// add-logging-settings-loader; spec logging/logging-settings).
//
// COLD PATH ONLY. `load_logging_settings` runs once before real-time
// execution: it reads an explicitly supplied global JSON file, then an
// explicitly supplied local JSON file, then applies environment overrides,
// merging field by field into `LoggerOptions`. Every problem becomes a
// `core::SettingsDiagnostic`; a bad value leaves the previous/default value in
// place so the caller can still initialize a logger.
//
// The loader never logs through the logging subsystem (the logger is not
// initialized yet) and never touches a producer hot path. `std::string`,
// `nlohmann::json` and the diagnostics vector are cold-path tools by design.
//
// Serialization mandate: member + free `to_string()` and `std::formatter` for
// `Settings`; the shared `SettingsSource`/`SettingsPaths`/`SettingsDiagnostic`
// vocabulary and its non-throwing JSON live in `nexigen::core`.
// `Settings` is reconstructed by the loader, so it provides `to_json`
// only — the absence of `from_json` is a compile-time-pinned contract.
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/settings_vocabulary.hpp>
#include <nexigen/logging/logger_options.hpp>

namespace nexigen::logging {

struct FileLoggerConfig; // defined by file_logger.hpp

/// Merged result of one load. `global_loaded`/`local_loaded` report file
/// presence, not success; `diagnostics` carries every non-fatal problem.
struct Settings {
    LoggerOptions options{};
    bool global_loaded{false};
    bool local_loaded{false};
    /// Explicit presence: only these values participate in the compile-time
    /// capacity check, so `LoggerOptions` defaults cannot reject a logger whose
    /// template limits are smaller.
    bool ring_capacity_set{false};
    bool max_threads_set{false};
    bool message_capacity_set{false};
    /// Canonical worker_thread presence. Console consumes canonical attributes
    /// but never inherits the legacy file-only affinity list.
    bool worker_thread_set{false};
    bool worker_cpus_set{false};
    std::vector<core::SettingsDiagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (body in src/logging/logging_settings.cpp)
void to_json(nlohmann::json& j, const Settings& settings);
// Deliberately no from_json(Settings&): reconstructed by the loader.

/// Read, merge and validate settings. `out` is always populated best-effort.
/// Returns Ok when no diagnostic was recorded, InvalidArgument when a value /
/// type / JSON problem was recorded, and SystemError when a supplied file
/// exists but cannot be read. Missing files are not errors.
[[nodiscard]] core::Status load_logging_settings(const core::SettingsPaths& paths,
                                                 Settings& out) noexcept;

/// Map merged settings into a file logger construction config. Returns
/// InvalidArgument when a mapped value violates the file logger's fixed-buffer
/// limits; the destination owns its strings.
[[nodiscard]] core::Status apply_to(const Settings& settings,
                                    FileLoggerConfig& out) noexcept;

/// Check explicitly configured capacities against a logger's compile-time
/// limits. Defaults that were never explicitly set are not checked. When
/// `diagnostics` is non-null, a Computed diagnostic is appended on failure.
template <std::size_t ShardCapacity, std::size_t MaxThreads, std::size_t MessageCapacity>
[[nodiscard]] core::Status check_capacity(
    const Settings& settings,
    std::vector<core::SettingsDiagnostic>* diagnostics = nullptr) noexcept {
    bool ok = true;
    if (settings.ring_capacity_set &&
        settings.options.spsc_ring_buffer_capacity > ShardCapacity) {
        ok = false;
        if (diagnostics != nullptr) {
            diagnostics->push_back(core::SettingsDiagnostic{
                core::SettingsSource::Computed,
                "spsc_ring_buffer_capacity",
                "configured " +
                    std::to_string(settings.options.spsc_ring_buffer_capacity) +
                    " exceeds logger shard capacity " +
                    std::to_string(ShardCapacity)});
        }
    }
    if (settings.max_threads_set &&
        settings.options.max_producer_threads > MaxThreads) {
        ok = false;
        if (diagnostics != nullptr) {
            diagnostics->push_back(core::SettingsDiagnostic{
                core::SettingsSource::Computed,
                "max_producer_threads",
                "configured " +
                    std::to_string(settings.options.max_producer_threads) +
                    " exceeds logger producer slots " +
                    std::to_string(MaxThreads)});
        }
    }
    if (settings.message_capacity_set &&
        settings.options.message_capacity > MessageCapacity) {
        ok = false;
        if (diagnostics != nullptr) {
            diagnostics->push_back(core::SettingsDiagnostic{
                core::SettingsSource::Computed,
                "message_capacity",
                "configured " +
                    std::to_string(settings.options.message_capacity) +
                    " exceeds logger message capacity " +
                    std::to_string(MessageCapacity)});
        }
    }
    if (!ok) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

} // namespace nexigen::logging


