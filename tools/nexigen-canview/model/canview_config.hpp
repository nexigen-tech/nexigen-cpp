// nexigen-canview — persisted configuration.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>

#include "can_view.hpp"
#include "filter_criteria.hpp"
#include "session_record.hpp"
#include "view_channel.hpp"

namespace nexigen::canview {

/// @brief Effective viewer configuration.
struct CanViewConfig {
    static constexpr std::size_t max_profiles{32U}; ///< Maximum channel profiles.
    static constexpr std::size_t max_enabled{8U};   ///< Maximum live-enabled channels.

    std::uint32_t version{1U};              ///< Config version.
    std::vector<CanViewChannel> channels{}; ///< Up to 32 channel profiles.
    std::vector<SessionEdsBinding> eds{};   ///< EDS bindings.
    std::uint32_t memory_mb{256U};          ///< Model memory budget.
    std::uint32_t view_count{1U};           ///< LIVE All views on startup.
    bool shared_timebase{false};            ///< Cross-channel timebase.
    ViewDefaults defaults{};                ///< Table/time defaults.
    FilterCriteria filter{};                ///< Initial filter.
    nlohmann::json views{nullptr};          ///< Persisted LIVE views.

    /// @brief Validate profile counts, names and identities.
    /// @return Empty status, or `InvalidArgument`.
    [[nodiscard]] nexigen::core::Status validate() const;
    /// @brief Number of enabled channels.
    /// @return Enabled profile count.
    [[nodiscard]] std::size_t enabled_count() const noexcept;
};

/// @brief Default XDG configuration path.
/// @return `$XDG_CONFIG_HOME/nexigen/canview.json` or a home fallback.
[[nodiscard]] std::string default_config_path();

/// @brief Load a configuration file.
/// @param path File path.
/// @param corrupted Receives `true` when the file was malformed.
/// @return Configuration; defaults on a missing/corrupted file.
[[nodiscard]] CanViewConfig load_config(const std::string& path, bool* corrupted = nullptr);

/// @brief Write a configuration atomically.
/// @param path Target path.
/// @param config Configuration.
/// @return Empty status, or `SystemError`.
[[nodiscard]] nexigen::core::Status save_config(const std::string& path,
                                                const CanViewConfig& config);

/// @brief Apply CLI overrides to a configuration.
/// @param config Destination.
/// @param options Parsed command line.
void apply_cli_overrides(CanViewConfig& config, const struct CliOptions& options) noexcept;

/// @brief Write a configuration as JSON.
void to_json(nlohmann::json& j, const CanViewConfig& config);
/// @brief Read a configuration from JSON without throwing.
void from_json(const nlohmann::json& j, CanViewConfig& config) noexcept;

} // namespace nexigen::canview
