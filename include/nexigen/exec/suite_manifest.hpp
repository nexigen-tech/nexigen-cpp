// nexigen::exec — parsed and validated suite manifest
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/exec/app_spec.hpp>

namespace nexigen::exec {

/// @brief One Function Group of the EM's own configuration (REQ_EM_005): its
///        name and the states it can be in.
///
/// Owned by `SuiteManifest` and copied into `FunctionGroupRegistry` at startup.
/// The list of states is the vocabulary a `SetState` target is validated
/// against; the group's current state is runtime data held by the registry,
/// not here.
///
/// @note Spec `exec/app-suite`, requirement "Function Group configuration".
struct FunctionGroupConfig {
    std::string name{};                ///< Group name, e.g. `ADAS`.
    std::vector<std::string> states{}; ///< States the group can be in.

    /// @brief Cold-path diagnostic rendering of the group configuration.
    ///
    /// @return Human-readable text naming the group and its states.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Parsed and validated suite manifest.
///
/// The declarative description of a suite: identity, roots, logging level,
/// restart budget, EM thread placement, Function Groups, and applications.
/// `load_suite_manifest` fills it and `validate_suite_manifest` checks it
/// before any process starts; the detached supervisor then holds it read-only
/// for the suite lifetime. Cold path only.
///
/// @note Spec `exec/app-suite`, requirement "Suite manifest has a fixed
///       schema".
/// @note Spec `exec/app-suite`, requirement "Suite manifest is validated
///       before launch".
/// @note Spec `exec/logging`, requirement "Manifest configures
///       execution_manager thread affinity".
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
struct SuiteManifest {
    std::uint32_t version{0};                            ///< Schema version; must be 1.
    std::string suite_id{};                              ///< Suite id; must be a safe identifier.
    std::string execution_manager_name{};                ///< EM identity; empty = `nexigen-exec`.
    std::string run_id{};                                ///< Run id shared by all applications.
    std::string run_dir{};                               ///< Absolute run directory.
    std::string global_settings_path{};                  ///< Absolute settings directory or empty.
    std::string artifacts_path{};                        ///< Absolute artifact root.
    std::optional<core::LogLevel> log_level{};           ///< Suite level; empty = settings value.
    std::uint32_t suite_restart_attempts{0};             ///< Shared whole-suite relaunch budget.
    std::uint64_t suite_restart_delay_s{5};              ///< Delay before a whole-suite relaunch.
    std::string manifest_path{};                         ///< Absolute manifest path.
    core::ThreadSpec execution_manager_control_thread{}; ///< Control-thread placement.
    core::ThreadSpec execution_manager_logger_thread{};  ///< Logger-worker placement.
    std::vector<FunctionGroupConfig> function_groups{};  ///< Declared Function Groups.
    std::vector<AppSpec> apps{};                         ///< Applications; must be non-empty.

    /// @brief Cold-path diagnostic rendering of the manifest.
    ///
    /// @return Human-readable text naming the suite and applications.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper (project mandate 9): mirrors the member.
///
/// @param value Manifest to render.
/// @return The same text as `value.to_string()`.
[[nodiscard]] inline std::string to_string(const SuiteManifest& value) {
    return value.to_string();
}

/// @brief Serialize the manifest into its JSON document.
///
/// @param j Destination JSON value.
/// @param manifest Manifest to serialize.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void to_json(nlohmann::json& j, const SuiteManifest& manifest);

/// @brief Parse a manifest from JSON without throwing.
///
/// Unknown fields are ignored; hostile or wrong-typed input leaves `manifest`
/// unchanged.
///
/// @param j Source JSON value.
/// @param manifest Target manifest.
/// @note Spec `exec/app-suite`, requirement "Suite manifest has a fixed
///       schema"; project mandate 10.
void from_json(const nlohmann::json& j, SuiteManifest& manifest) noexcept;

/// @brief Read, parse, and validate one suite manifest.
///
/// `manifest_path` is resolved to an absolute path so relative local settings
/// resolve deterministically. Parsing is non-throwing; validation runs after
/// parsing and completes before any process starts.
///
/// @param path Manifest path supplied on the command line.
/// @param out Receives the parsed manifest; reset before parsing.
/// @return Success, `InvalidArgument` for a malformed or invalid manifest, or
///         `SystemError` when the file cannot be read.
/// @note Spec `exec/app-suite`, requirement "Suite manifest is validated
///       before launch".
/// @note Spec `exec/app-suite`, scenario "Validation failure starts nothing".
[[nodiscard]] core::Status load_suite_manifest(std::string_view path, SuiteManifest& out) noexcept;

/// @brief Pure validation over an already parsed manifest.
///
/// Rejects a wrong version, missing required fields, an empty `apps` array,
/// duplicate `(app, instance)` pairs, unsafe identifiers, relative
/// `run_dir`/`artifacts_path`, invalid settings paths, unknown impact or log
/// level values, negative counts, and invalid or offline CPU sets. Performs no
/// I/O and starts nothing.
///
/// @param manifest Parsed manifest to check.
/// @return Success or `InvalidArgument` naming the first violated rule.
/// @note Spec `exec/app-suite`, requirement "Suite manifest is validated
///       before launch".
/// @note Spec `exec/app-suite`, scenario "CPU index outside the online set is rejected".
[[nodiscard]] core::Status validate_suite_manifest(const SuiteManifest& manifest) noexcept;

} // namespace nexigen::exec
