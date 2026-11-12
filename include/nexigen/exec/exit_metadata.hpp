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

/// @brief Exit metadata written to `exit.json`.
///
/// Records the outcome of one reaped child: exactly one of `exit_code` and
/// `signal` is set, the wall-clock uptime, and who caused the termination.
/// Written by the instance supervisor or by a stop finalizer after reaping,
/// and read by status. Cold path only.
///
/// @note Spec `exec/instance-lifecycle`, requirement "exit.json captures exit
///       metadata".
/// @note Spec `exec/process-supervision`, requirement "Unreapable child errors
///       do not fabricate success".
struct ExitMetadata {
    std::optional<std::int32_t> exit_code{};           ///< null when killed by a signal.
    std::optional<std::int32_t> signal{};              ///< null on a normal exit.
    std::uint64_t uptime_s{0};                         ///< Whole seconds from start to exit.
    TerminatedBy terminated_by{TerminatedBy::Unknown}; ///< Termination attribution.

    /// @brief Cold-path diagnostic rendering of the exit metadata.
    ///
    /// @return Human-readable text naming the exit code or signal and the
    ///         attribution.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;

    /// @brief Build exit metadata from a `waitpid` status word.
    ///
    /// `WIFEXITED` sets `exit_code`; `WIFSIGNALED` sets `signal`. When neither
    /// predicate holds, both optional fields stay empty and the caller is
    /// expected to pass `TerminatedBy::Unknown`.
    ///
    /// @param pid Child pid the status belongs to (diagnostics only).
    /// @param status Raw `waitpid` status word.
    /// @param uptime_s Wall-clock seconds between start and exit.
    /// @param terminated_by Attribution chosen by the caller's stop logic.
    /// @return The populated record.
    /// @note Spec `exec/instance-lifecycle`, requirement "exit.json captures
    ///       exit metadata".
    [[nodiscard]] static ExitMetadata from_child_status(std::int32_t pid, std::int32_t status,
                                                        std::uint64_t uptime_s,
                                                        TerminatedBy terminated_by) noexcept;
};

/// @brief Free helper (project mandate 9): mirrors the member.
///
/// @param value Exit metadata to render.
/// @return The same text as `value.to_string()`.
[[nodiscard]] inline std::string to_string(const ExitMetadata& value) {
    return value.to_string();
}

/// @brief Serialize exit metadata into the `exit.json` document.
///
/// Absent optional fields become JSON `null`.
///
/// @param j Destination JSON value.
/// @param metadata Record to serialize.
/// @note Spec `exec/instance-lifecycle`, requirement "resource.json and
///       exit.json are serializable"; project mandate 10.
void to_json(nlohmann::json& j, const ExitMetadata& metadata);

/// @brief Parse exit metadata from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `metadata` unchanged.
///
/// @param j Source JSON value.
/// @param metadata Target record.
/// @note Spec `exec/instance-lifecycle`, scenario "Hostile JSON leaves
///       metadata unchanged"; project mandate 10.
void from_json(const nlohmann::json& j, ExitMetadata& metadata) noexcept;

/// @brief Write `<instance_dir>/exit.json` (completion metadata).
///
/// The write is atomic and is the idempotence marker for `stop`: an instance
/// that already has `exit.json` is left untouched.
///
/// @param instance_dir Control instance directory.
/// @param metadata Record to write.
/// @return Success, `InvalidArgument` for a bad path, or `SystemError` when the
///         file cannot be written.
/// @note Spec `exec/instance-lifecycle`, requirement "exit.json captures exit
///       metadata".
[[nodiscard]] core::Status write_exit_json(std::string_view instance_dir,
                                           const ExitMetadata& metadata) noexcept;

} // namespace nexigen::exec
