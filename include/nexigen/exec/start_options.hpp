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

/// Optional launch settings for one instance. Defaults preserve the original
/// single-instance behavior: streams in the instance directory, no extra
/// environment, no affinity restriction, no settings sources, no level override.
struct StartOptions {
    std::string artifact_dir{};                             ///< Empty = instance directory.
    std::string global_settings_dir{};                      ///< Absolute directory or empty.
    std::string local_settings_dir{};                       ///< Absolute directory or empty.
    std::optional<core::LogLevel> log_level{};              ///< Empty = keep settings value.
    std::vector<std::pair<std::string, std::string>> env{}; ///< Child-only env.
    core::CpuSet cpus{};                                    ///< Empty = unrestricted.

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] inline std::string to_string(const StartOptions& options) {
    return options.to_string();
}

} // namespace nexigen::exec
