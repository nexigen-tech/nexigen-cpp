// nexigen::chrono — provider configuration (change add-chrono-clock-service;
// spec chrono/chrono-provider; extended with `kind` by change
// add-clock-authority-core).
//
// Cold-path diagnostics only: to_string allocates; to_json/from_json bodies
// live in src/chrono/provider_config.cpp so consumer TUs stay lean.
#pragma once

#include <chrono>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/provider_kind.hpp>
#include <nexigen/core/thread_management.hpp>

namespace nexigen::chrono {

/// @brief Configuration for constructing a `Provider`: period, thread spec, and mode.
///
/// `collect_interval` drives periodic snapshot delivery (zero disables the
/// background thread and leaves explicit `snapshot_now()` as the only trigger);
/// `worker_thread` carries the complete background-thread specification; and
/// `kind` selects daemon versus manual drive. The value is cold-path data only:
/// no provider hot path reads it.
///
/// @note Spec `chrono/provider`, requirement "Provider configuration
///       serialization"; spec `chrono/chrono-settings`, requirement "Merged
///       settings apply to provider configuration".
struct ProviderConfig {
    std::chrono::nanoseconds collect_interval{0}; ///< Snapshot period; zero disables the worker.
    core::ThreadSpec worker_thread{};             ///< Background thread name/affinity/scheduler.
    ProviderKind kind{ProviderKind::daemon};      ///< Daemon worker or manual trigger mode.

    /// @brief Render this configuration as a diagnostic string.
    ///
    /// The body lives in `src/chrono/provider_config.cpp`.
    ///
    /// @return The rendered text naming interval, thread spec, and kind.
    /// @note Spec `chrono/provider`, requirement "Provider configuration
    ///       serialization"; project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free-helper mirror of `ProviderConfig::to_string`.
///
/// @param config Configuration to render.
/// @return The same text as `config.to_string()`.
/// @note Spec `chrono/provider`, requirement "Provider configuration
///       serialization"; project mandate 9.
[[nodiscard]] std::string to_string(const ProviderConfig& config);

/// @brief Serialize the complete provider configuration as JSON (cold path).
///
/// Carries the interval in nanoseconds, every worker attribute, and the kind.
/// The definition lives in `src/chrono/provider_config.cpp`.
///
/// @param j Destination JSON value, replaced by the serialized configuration.
/// @param config Configuration to serialize.
/// @note Spec `chrono/provider`, requirement "Provider configuration
///       serialization"; project mandate 10.
void to_json(nlohmann::json& j, const ProviderConfig& config);
/// @brief Parse a provider configuration from JSON, strictly non-throwing.
///
/// Under `JSON_NOEXCEPTION=1` a non-object value leaves `out` unchanged, and a
/// missing or mistyped field is skipped so that field keeps its previous value.
/// The reader accepts the current `worker_thread` object, the legacy
/// `core_mask` affinity alias, `collect_interval_ns`, and `kind`. The
/// definition lives in `src/chrono/provider_config.cpp`.
///
/// @param j Source JSON value.
/// @param out Destination configuration; each recognized field is assigned in
///        place.
/// @note Spec `chrono/provider`, requirement "Provider configuration
///       serialization"; project mandate 10.
void from_json(const nlohmann::json& j, ProviderConfig& out) noexcept;

} // namespace nexigen::chrono

/// @brief `std::formatter` specialization rendering the configuration diagnostically.
///
/// @note Spec `chrono/provider`, requirement "Provider configuration
///       serialization"; project mandate 11.
template <>
struct std::formatter<nexigen::chrono::ProviderConfig> : std::formatter<std::string_view> {
    /// @brief Format a provider configuration as its diagnostic string.
    ///
    /// @tparam Ctx Format context type; the inherited string specifiers apply.
    /// @param config Configuration to format.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    /// @note Project mandate 11 (`std::formatter`).
    template <class Ctx>
    auto format(const nexigen::chrono::ProviderConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
