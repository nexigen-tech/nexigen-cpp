// nexigen::logging — global/local logging settings loader (change
// add-logging-settings-loader; spec logging/logging-settings).
//
// COLD PATH ONLY. `load_logging_settings` runs once before real-time
// execution: it reads an explicitly supplied global JSON file, then an
// explicitly supplied local JSON file, then applies environment overrides,
// merging field by field into `LoggerOptions`. Every problem becomes a
// `Settings_diagnostic`; a bad value leaves the previous/default value in
// place so the caller can still initialize a logger.
//
// The loader never logs through the logging subsystem (the logger is not
// initialized yet) and never touches a producer hot path. `std::string`,
// `nlohmann::json` and the diagnostics vector are cold-path tools by design.
//
// Serialization mandate: member + free `to_string()` and `std::formatter` for
// every type; non-throwing ADL `to_json`/`from_json` under
// `JSON_NOEXCEPTION=1` for `Settings_paths`/`Settings_diagnostic`.
// `Logging_settings` is reconstructed by the loader, so it provides `to_json`
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
#include <nexigen/logging/logger_options.hpp>

namespace nexigen::logging {

struct FileLogger_config; // defined by file_logger.hpp

/// Which source produced a value or a diagnostic.
enum class Settings_source : std::uint8_t {
    Global,      ///< Global JSON file.
    Local,       ///< Local JSON file.
    Environment, ///< NEXIGEN_LOG_* override.
    Computed,    ///< Derived check (capacity limits).
};

[[nodiscard]] constexpr std::string_view to_string(Settings_source src) noexcept {
    switch (src) {
        case Settings_source::Global:      return "global";
        case Settings_source::Local:       return "local";
        case Settings_source::Environment: return "environment";
        case Settings_source::Computed:    return "computed";
        default:                           return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         Settings_source& out) noexcept {
    if (name == "global")      { out = Settings_source::Global; return true; }
    if (name == "local")       { out = Settings_source::Local; return true; }
    if (name == "environment") { out = Settings_source::Environment; return true; }
    if (name == "computed")    { out = Settings_source::Computed; return true; }
    return false;
}

inline void to_json(nlohmann::json& j, const Settings_source& src) {
    j = std::string(to_string(src));
}

inline void from_json(const nlohmann::json& j, Settings_source& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// Explicit source paths. An empty path disables that source; the loader never
/// consults implicit locations (/etc, XDG, executable directory).
struct Settings_paths {
    std::string global_file{};
    std::string local_file{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Settings_paths& paths) {
    return paths.to_string();
}

/// One best-effort finding: which source, which dotted field, what went wrong.
struct Settings_diagnostic {
    Settings_source source{Settings_source::Global};
    std::string field{};
    std::string message{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Settings_diagnostic& diag) {
    return diag.to_string();
}

/// Merged result of one load. `global_loaded`/`local_loaded` report file
/// presence, not success; `diagnostics` carries every non-fatal problem.
struct Logging_settings {
    LoggerOptions options{};
    bool global_loaded{false};
    bool local_loaded{false};
    /// Explicit presence: only these values participate in the compile-time
    /// capacity check, so `LoggerOptions` defaults cannot reject a logger whose
    /// template limits are smaller.
    bool ring_capacity_set{false};
    bool max_threads_set{false};
    bool message_capacity_set{false};
    std::vector<Settings_diagnostic> diagnostics{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Logging_settings& settings) {
    return settings.to_string();
}

// --- cold-path ADL serialization (bodies in src/logging/logging_settings.cpp)
void to_json(nlohmann::json& j, const Settings_paths& paths);
void from_json(const nlohmann::json& j, Settings_paths& out) noexcept;

void to_json(nlohmann::json& j, const Settings_diagnostic& diag);
void from_json(const nlohmann::json& j, Settings_diagnostic& out) noexcept;

void to_json(nlohmann::json& j, const Logging_settings& settings);
// Deliberately no from_json(Logging_settings&): reconstructed by the loader.

/// Read, merge and validate settings. `out` is always populated best-effort.
/// Returns Ok when no diagnostic was recorded, InvalidArgument when a value /
/// type / JSON problem was recorded, and SystemError when a supplied file
/// exists but cannot be read. Missing files are not errors.
[[nodiscard]] core::Status load_logging_settings(const Settings_paths& paths,
                                                 Logging_settings& out) noexcept;

/// Map merged settings into a file logger construction config. String views in
/// `out` alias `settings.options` storage: `settings` must outlive `out` and
/// the subsequent initialize() call. Returns InvalidArgument when a mapped
/// value violates the file logger's fixed-buffer limits.
[[nodiscard]] core::Status apply_to(const Logging_settings& settings,
                                    FileLogger_config& out) noexcept;

/// Check explicitly configured capacities against a logger's compile-time
/// limits. Defaults that were never explicitly set are not checked. When
/// `diagnostics` is non-null, a Computed diagnostic is appended on failure.
template <std::size_t ShardCapacity, std::size_t MaxThreads, std::size_t MessageCapacity>
[[nodiscard]] core::Status check_capacity(
    const Logging_settings& settings,
    std::vector<Settings_diagnostic>* diagnostics = nullptr) noexcept {
    bool ok = true;
    if (settings.ring_capacity_set &&
        settings.options.spsc_ring_buffer_capacity > ShardCapacity) {
        ok = false;
        if (diagnostics != nullptr) {
            diagnostics->push_back(Settings_diagnostic{
                Settings_source::Computed,
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
            diagnostics->push_back(Settings_diagnostic{
                Settings_source::Computed,
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
            diagnostics->push_back(Settings_diagnostic{
                Settings_source::Computed,
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

template <>
struct std::formatter<nexigen::logging::Settings_source>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::logging::Settings_source src, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::logging::to_string(src), ctx);
    }
};

template <>
struct std::formatter<nexigen::logging::Settings_paths>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::logging::Settings_paths& paths, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(paths.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::logging::Settings_diagnostic>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::logging::Settings_diagnostic& diag, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(diag.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::logging::Logging_settings>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::logging::Logging_settings& settings, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(settings.to_string(), ctx);
    }
};
