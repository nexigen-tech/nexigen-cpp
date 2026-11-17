// nexigen::com — per-service E2E configuration (change add-e2e-protection).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/com/e2e/e2e_profile.hpp>
#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::com {

/// @brief E2E section of one service entry.
///
/// `profile == None` selects plain communication: the application uses the
/// ordinary ports. An unknown profile name is reported through
/// `ServiceManifest::diagnostics`, not stored here.
///
/// @note Spec `com/e2e`, requirement "E2E service configuration".
struct E2eConfig {
    E2eProfile profile{E2eProfile::None}; ///< Selected profile.
    std::uint32_t data_id{0};             ///< Service-specific identity; required when protected.
    std::uint32_t max_delta_counter{10};  ///< Largest accepted counter gap.

    /// @brief Cold-path diagnostic rendering.
    /// @return `E2eConfig(profile=..., data_id=..., max_delta_counter=...)`.
    /// @note Spec `com/e2e`, requirement "E2E service configuration".
    [[nodiscard]] std::string to_string() const {
        return std::format("E2eConfig(profile={}, data_id={}, max_delta_counter={})",
                           nexigen::com::to_string(profile), data_id, max_delta_counter);
    }
};

/// @brief Free helper mirroring `E2eConfig::to_string()` (mandate 9).
///
/// @param config Configuration to render.
/// @return The diagnostic string.
/// @note Spec `com/e2e`, requirement "E2E service configuration".
[[nodiscard]] inline std::string to_string(const E2eConfig& config) {
    return config.to_string();
}

/// @brief ADL JSON writer for the E2E section.
///
/// @param j Output JSON object, replaced by the canonical section fields.
/// @param config Section to serialize.
/// @note Spec `com/e2e`, requirement "E2E service configuration"; mandate 10.
inline void to_json(nlohmann::json& j, const E2eConfig& config) {
    j = nlohmann::json{{"profile", nexigen::com::to_string(config.profile)},
                       {"data_id", config.data_id},
                       {"max_delta_counter", config.max_delta_counter}};
}

/// @brief ADL JSON reader for the E2E section (non-throwing).
///
/// An absent section keeps the destination unchanged. An unknown profile name
/// leaves `profile` at `None`; `ServiceManifest::from_json` records the finding
/// in `ServiceManifest::diagnostics` so validation can reject it.
///
/// @param j Input JSON value; non-object values leave `config` unchanged.
/// @param config Destination section.
/// @note Spec `com/e2e`, requirement "E2E service configuration"; mandate 10.
inline void from_json(const nlohmann::json& j, E2eConfig& config) noexcept {
    if (!j.is_object()) {
        return;
    }
    std::string name;
    if (core::detail::read_string(j, "profile", name)) {
        static_cast<void>(nexigen::com::from_string(name, config.profile));
    }
    core::detail::read_unsigned(j, "data_id", config.data_id);
    core::detail::read_unsigned(j, "max_delta_counter", config.max_delta_counter);
}

/// @brief Whether a parsed section is usable.
///
/// @param config Section to check.
/// @return `false` when a protected service has no data id or the maximum
///         delta is zero. Unknown profile names are reported through
///         `ServiceManifest::diagnostics`.
[[nodiscard]] inline bool valid_e2e_config(const E2eConfig& config) noexcept {
    if (config.max_delta_counter == 0U) {
        return false;
    }
    return config.profile == E2eProfile::None || config.data_id != 0U;
}

} // namespace nexigen::com

/// @brief `std::formatter` for the E2E section (project mandate 11).
template <> struct std::formatter<nexigen::com::E2eConfig> : std::formatter<std::string_view> {
    /// @brief Write the section diagnostic into the format context.
    /// @tparam Ctx `std::format` context type.
    /// @param config Section to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Spec `com/e2e`, requirement "E2E service configuration".
    template <class Ctx> auto format(const nexigen::com::E2eConfig& config, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(config.to_string(), ctx);
    }
};
