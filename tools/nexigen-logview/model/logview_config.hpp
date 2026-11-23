// nexigen-logview — persisted viewer configuration.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/log_level.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>

#include "color_manager.hpp"
#include "delta_mode.hpp"
#include "timestamp_mode.hpp"

namespace nexigen::logview {

/// @brief Every setting the viewer persists.
struct LogViewConfig {
    /// @brief Log service to attach to.
    std::string service_name{"Nexigen/Log/nexigen"};
    /// @brief Hard model memory budget in MiB.
    std::uint32_t max_memory_mb{256};
    /// @brief Process CPU share that triggers ECO, in percent.
    std::uint32_t cpu_limit_percent{50};
    /// @brief Incoming rate that triggers ECO, in messages per second.
    std::uint64_t storm_rate{100000};
    /// @brief Delta magnitude highlighted as anomalous, in nanoseconds.
    std::uint64_t anomaly_threshold_ns{50'000'000};
    /// @brief Timestamp display mode.
    TimestampMode timestamp_mode{TimestampMode::Absolute};
    /// @brief Delta computation mode.
    DeltaMode delta_mode{DeltaMode::PreviousVisible};
    /// @brief Per-level visibility (Verbose..Off).
    std::array<bool, 7> level_visible{true, true, true, true, true, true, true};
    /// @brief Initial text filter.
    std::string text_filter{};
    /// @brief Application codes hidden at startup.
    std::vector<std::string> hidden_apps{};
    /// @brief Level and application colors.
    ColorManager colors{};
    /// @brief Capture thread placement requested by the configuration.
    nexigen::core::ThreadSpec capture_thread{};
    /// @brief Export thread placement requested by the configuration.
    nexigen::core::ThreadSpec export_thread{};

    /// @brief Validate ranges and thread specs before startup.
    /// @return `Status{}` when usable.
    [[nodiscard]] nexigen::core::Status validate() const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the principal settings.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic mirror of `LogViewConfig::to_string`.
/// @param config Config to render.
/// @return The same text.
[[nodiscard]] inline std::string to_string(const LogViewConfig& config) {
    return config.to_string();
}

/// @brief ADL JSON writer.
/// @param j Destination object.
/// @param config Config to serialize.
void to_json(nlohmann::json& j, const LogViewConfig& config);

/// @brief Non-throwing ADL JSON reader.
/// @param j Source object.
/// @param config Destination; absent or ill-typed fields remain unchanged.
void from_json(const nlohmann::json& j, LogViewConfig& config) noexcept;

/// @brief Resolve the default configuration path.
///
/// Uses `$XDG_CONFIG_HOME/nexigen/logview.json`, falling back to
/// `$HOME/.config/nexigen/logview.json`, then `./logview.json`.
///
/// @return Absolute or relative path; never throws.
[[nodiscard]] std::string default_config_path();

/// @brief Load configuration from disk.
///
/// A missing file is not an error and leaves the defaults. A corrupt file
/// leaves the defaults, sets `warning`, and remains untouched on disk.
///
/// @param path Configuration file.
/// @param config Destination.
/// @param warning True when the file existed but could not be parsed.
/// @return `Status{}` for success or a missing file; `InvalidArgument` for a
///         corrupt document.
[[nodiscard]] nexigen::core::Status load_config(const std::string& path, LogViewConfig& config,
                                                bool& warning) noexcept;

/// @brief Persist configuration atomically (temporary file plus rename).
/// @param path Destination file.
/// @param config Configuration to write.
/// @return `Status{}` on success; `SystemError` on any I/O failure.
[[nodiscard]] nexigen::core::Status save_config(const std::string& path,
                                                const LogViewConfig& config) noexcept;

} // namespace nexigen::logview

/// @brief `std::formatter` for `LogViewConfig`.
template <>
struct std::formatter<nexigen::logview::LogViewConfig> : std::formatter<std::string_view> {
    /// @brief Format the diagnostic text.
    template <class Ctx>
    auto format(const nexigen::logview::LogViewConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
