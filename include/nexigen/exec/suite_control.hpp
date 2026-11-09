// nexigen::exec — detached suite lifecycle control
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec {

/// Start a detached suite supervisor. Returns once the supervisor owns the
/// suite and wrote `suite.json`. Per-application launch failures are recorded
/// in suite state and handled by policy, not by this status.
[[nodiscard]] core::Status start_suite(const SuiteManifest& manifest, std::string& suite_dir_out);

/// Request a final suite stop and wait for the recorded outcome. Idempotent.
[[nodiscard]] core::Status
stop_suite(std::string_view suite_dir,
           std::optional<std::uint64_t> grace_override = std::nullopt) noexcept;

} // namespace nexigen::exec
