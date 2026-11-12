// nexigen::exec — optional launch settings for one instance
// (change refactor-execution_manager-structure; spec execution_manager/process-supervision).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/log_level.hpp>

namespace nexigen::exec {

/// @brief Optional launch settings for one instance.
///
/// Passed to `ExecutionManager::start` by suite supervision to relocate
/// streams, layer settings sources, override the log level, add child-only
/// environment entries, and restrict affinity. Defaults preserve the original
/// single-instance behavior: streams in the instance directory, no extra
/// environment, no affinity restriction, no settings sources, no level
/// override. A plain value type consumed before `fork`.
///
/// @note Spec `exec/process-supervision`, requirement "Child environment and
///       CPU affinity are configurable".
/// @note Spec `exec/app-suite`, requirement "Manifest launch settings reach
///       each child".
struct StartOptions {
    std::string artifact_dir{};                ///< Stream root; empty = instance directory.
    std::string global_settings_dir{};         ///< Absolute directory or empty.
    std::string local_settings_dir{};          ///< Absolute directory or empty.
    std::optional<core::LogLevel> log_level{}; ///< Empty = keep settings value.
    std::vector<std::pair<std::string, std::string>> env{}; ///< Child-only env.
    core::CpuSet cpus{}; ///< Affinity request; empty = unrestricted.

    /// @brief Cold-path diagnostic rendering of the launch options.
    ///
    /// @return Human-readable text naming the artifact root and overrides.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper (project mandate 9): mirrors the member.
///
/// @param options Launch options to render.
/// @return The same text as `options.to_string()`.
[[nodiscard]] inline std::string to_string(const StartOptions& options) {
    return options.to_string();
}

} // namespace nexigen::exec
