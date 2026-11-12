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
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/settings_diagnostic.hpp>
#include <nexigen/core/settings_paths.hpp>
#include <nexigen/core/settings_source.hpp>
#include <nexigen/logging/logger_options.hpp>

namespace nexigen::logging {

/// @brief Merged result of one `load_logging_settings` call.
///
/// Holds the merged `LoggerOptions` plus the per-source presence flags and every
/// non-fatal problem encountered. `global_loaded`/`local_loaded` report file
/// presence, not success; `diagnostics` carries each problem in the order it was
/// found. The struct is cold-path only and owns its strings and diagnostics
/// vector.
///
/// @note Spec `logging/logging-settings`, requirement "Invalid input degrades to best effort".
struct Settings {
    /// @brief Options merged from global file, local file, and environment, in that
    ///        precedence order.
    LoggerOptions options{};
    /// @brief Whether the global file existed and was read.
    bool global_loaded{false};
    /// @brief Whether the local file existed and was read.
    bool local_loaded{false};
    /// @brief Whether `options.spsc_ring_buffer_capacity` was set explicitly.
    ///
    /// Only explicitly present values participate in the compile-time capacity
    /// check, so `LoggerOptions` defaults cannot reject a logger whose template
    /// limits are smaller.
    bool ring_capacity_set{false};
    /// @brief Whether `options.max_producer_threads` was set explicitly.
    bool max_threads_set{false};
    /// @brief Whether `options.message_capacity` was set explicitly.
    bool message_capacity_set{false};
    /// @brief Whether a canonical `worker_thread` spec was configured.
    ///
    /// Console consumes canonical attributes but never inherits the legacy
    /// file-only affinity list.
    bool worker_thread_set{false};
    /// @brief Whether canonical worker CPUs were configured explicitly.
    ///
    /// Distinguishes "no CPU constraint" from "canonical CPUs deliberately
    /// cleared".
    bool worker_cpus_set{false};
    /// @brief Every non-fatal problem found while loading, in discovery order.
    std::vector<core::SettingsDiagnostic> diagnostics{};

    /// @brief Cold-path diagnostic rendering of the merged settings.
    ///
    /// @return Non-empty text naming each field, flag, and recorded diagnostic.
    /// @note Project mandate 9 (`to_string` on every public value type); the body lives in
    ///       `settings.cpp`.
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free `to_string` entry point for the merged settings (mandate 9).
///
/// @param settings Settings to render.
/// @return Exactly `settings.to_string()`.
/// @note Project mandate 9; ADL-friendly mirror of the member function.
[[nodiscard]] inline std::string to_string(const Settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (body in src/logging/settings.cpp)
/// @brief Cold-path JSON writer for the merged settings (mandate 10).
///
/// @param j Destination JSON object; overwritten with the settings' field map.
/// @param settings Settings to serialize.
/// @note Project mandate 10. There is deliberately no `from_json(Settings&)`:
///       the loader reconstructs the struct, and the absence is a
///       compile-time-pinned contract.
void to_json(nlohmann::json& j, const Settings& settings);

/// @brief Read, merge, and validate the logging settings.
///
/// Reads the global file, then the local file, then applies environment
/// overrides, merging field by field into `out`. Missing files are not errors;
/// every problem is recorded as a `core::SettingsDiagnostic` and the affected
/// value keeps its previous/default contents.
///
/// @param paths Explicit global/local paths supplied by the caller.
/// @param out Destination; always populated best-effort, even on failure.
/// @return Empty `core::Status` when no diagnostic was recorded,
///         `ErrorCode::InvalidArgument` when a value, type, or JSON problem was
///         recorded, and `ErrorCode::SystemError` when a supplied file exists but
///         cannot be read.
/// @note Spec `logging/logging-settings`, requirement "Settings sources are explicit".
/// @note Spec `logging/logging-settings`, requirement "Merge precedence is deterministic".
/// @note Spec `logging/logging-settings`, requirement "Settings loading is cold path only".
[[nodiscard]] core::Status load_logging_settings(const core::SettingsPaths& paths,
                                                 Settings& out) noexcept;

/// @brief Check explicitly configured capacities against a logger's compile-time limits.
///
/// Values that were never explicitly set (the `*_set` flags are false) are not
/// checked, so documented `LoggerOptions` defaults never reject a logger whose
/// template limits are smaller.
///
/// @tparam ShardCapacity Compile-time shard ring capacity of the target logger.
/// @tparam MaxThreads Compile-time producer slot count of the target logger.
/// @tparam MessageCapacity Compile-time inline message capacity of the target logger.
/// @param settings Merged settings to validate.
/// @param diagnostics Optional sink; when non-null, a `Computed` diagnostic is appended
///        for each excess value.
/// @return Empty `core::Status` when every explicit value fits, otherwise
///         `ErrorCode::InvalidArgument`.
/// @note Spec `logging/logging-settings`, requirement
///       "Explicit compile-time capacity limits are checked".
/// @note Spec `logging/logging-settings`, requirement "Default capacities are never checked".
template <std::size_t ShardCapacity, std::size_t MaxThreads, std::size_t MessageCapacity>
[[nodiscard]] core::Status
check_capacity(const Settings& settings,
               std::vector<core::SettingsDiagnostic>* diagnostics = nullptr) noexcept {
    bool ok = true;
    if (settings.ring_capacity_set && settings.options.spsc_ring_buffer_capacity > ShardCapacity) {
        ok = false;
        if (diagnostics != nullptr) {
            diagnostics->push_back(core::SettingsDiagnostic{
                core::SettingsSource::Computed, "spsc_ring_buffer_capacity",
                "configured " + std::to_string(settings.options.spsc_ring_buffer_capacity) +
                    " exceeds logger shard capacity " + std::to_string(ShardCapacity)});
        }
    }
    if (settings.max_threads_set && settings.options.max_producer_threads > MaxThreads) {
        ok = false;
        if (diagnostics != nullptr) {
            diagnostics->push_back(core::SettingsDiagnostic{
                core::SettingsSource::Computed, "max_producer_threads",
                "configured " + std::to_string(settings.options.max_producer_threads) +
                    " exceeds logger producer slots " + std::to_string(MaxThreads)});
        }
    }
    if (settings.message_capacity_set && settings.options.message_capacity > MessageCapacity) {
        ok = false;
        if (diagnostics != nullptr) {
            diagnostics->push_back(core::SettingsDiagnostic{
                core::SettingsSource::Computed, "message_capacity",
                "configured " + std::to_string(settings.options.message_capacity) +
                    " exceeds logger message capacity " + std::to_string(MessageCapacity)});
        }
    }
    if (!ok) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

} // namespace nexigen::logging
