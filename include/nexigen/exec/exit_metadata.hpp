// nexigen::exec — completion metadata written to exit.json
// (change refactor-execution_manager-structure; spec execution_manager/instance-lifecycle).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/exec/terminated_by.hpp>

namespace nexigen::exec {

/// Exit metadata written to exit.json.
struct ExitMetadata {
    std::optional<std::int32_t> exit_code{}; ///< null when killed by a signal.
    std::optional<std::int32_t> signal{};    ///< null on a normal exit.
    std::uint64_t uptime_s{0};               ///< Whole seconds from start to exit.
    TerminatedBy terminated_by{TerminatedBy::Unknown};

    [[nodiscard]] std::string to_string() const;

    /// Build from a waitpid status word (WIFEXITED/WIFSIGNALED are honoured).
    [[nodiscard]] static ExitMetadata from_child_status(std::int32_t pid, std::int32_t status,
                                                        std::uint64_t uptime_s,
                                                        TerminatedBy terminated_by) noexcept;
};

[[nodiscard]] inline std::string to_string(const ExitMetadata& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const ExitMetadata& metadata);
void from_json(const nlohmann::json& j, ExitMetadata& metadata) noexcept;

/// Write `<instance_dir>/exit.json` (completion metadata).
[[nodiscard]] core::Status write_exit_json(std::string_view instance_dir,
                                           const ExitMetadata& metadata) noexcept;

} // namespace nexigen::exec
