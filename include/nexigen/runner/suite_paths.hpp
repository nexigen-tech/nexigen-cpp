// nexigen::runner — deterministic suite/attempt path contract
// (change refactor-runner-structure; spec runner/app-suite).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace nexigen::runner {

/// `<run_dir>/.suite/<suite_id>`.
[[nodiscard]] std::string suite_dir_path(std::string_view run_dir, std::string_view suite_id);

/// `<artifacts_path>/<suite_id>/<app>/<instance>/attempt-<NNNN>`.
[[nodiscard]] std::string attempt_dir_path(std::string_view artifacts_path,
                                           std::string_view suite_id, std::string_view app,
                                           std::string_view instance, std::uint32_t attempt);

} // namespace nexigen::runner
