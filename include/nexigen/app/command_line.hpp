// nexigen::app — cold-path command-line contract (spec app/command-line).
// Entity structs (CommandLineDiagnostic/Options/Error) were split into their
// own headers; this file retains the parser and the path/apply/load helpers.
#pragma once

#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/app/app_settings.hpp>
#include <nexigen/app/app_settings_paths.hpp>
#include <nexigen/app/command_line_diagnostic.hpp>
#include <nexigen/app/command_line_error.hpp>
#include <nexigen/app/command_line_options.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/log_level.hpp>

namespace nexigen::app {

/// @brief Outcome of parsing: the accepted options or a diagnostic payload.
///
/// @note Spec `app/command-line`, requirement "Unknown, missing, repeated, and
///       positional arguments are handled".
using CommandLineResult = core::Result<CommandLineOptions, CommandLineError>;

/// @brief Parse process arguments, skipping the program name.
///
/// `argv[0]` is never interpreted as an option; every later element is parsed.
/// `-h`/`--help` short-circuits and accepts otherwise invalid input.
///
/// @param argc Argument count as passed to `main`; values `<= 1` yield an empty
///        argument list (subject to the settings-directory requirement).
/// @param argv Argument vector as passed to `main`; the first element is
///        skipped, and a null vector is treated as an empty argument list.
/// @return The parsed options, or `InvalidArgument` with one diagnostic per
///         finding.
/// @note Spec `app/command-line`, requirement "Long and short option forms are
///       supported".
/// @note Spec `app/command-line`, requirement "Help short-circuits with
///       success".
/// @note Cold path only: never called from a tick or worker loop.
[[nodiscard]] CommandLineResult parse_command_line(int argc, const char* const argv[]) noexcept;

/// @brief Parse already-split arguments, treating every element as an option.
///
/// Unlike the `argv` overload there is no program name to skip.
///
/// @param args Argument spellings to parse, in command-line order.
/// @return The parsed options, or `InvalidArgument` with one diagnostic per
///         finding.
/// @note Spec `app/command-line`, requirement "Long and short option forms are
///       supported".
/// @note Spec `app/command-line`, requirement "Option parsing honors the
///       terminator".
/// @note Cold path only.
[[nodiscard]] CommandLineResult parse_command_line(std::span<const std::string_view> args) noexcept;

/// @brief Map the two directories to the four fixed global/local file pairs.
///
/// Each supplied directory is joined with `logging.json`, `metrics.json`,
/// `tracing.json` and `chrono.json`; a trailing slash never produces a double
/// slash.
///
/// @param options Parsed directories; an empty entry leaves that source pair
///        empty (disabled).
/// @return The four path pairs, with absent sources left empty.
/// @note Spec `app/command-line`, requirement "Fixed file names produce the
///       four settings pairs".
/// @note Cold path only: performs string work, no I/O.
[[nodiscard]] AppSettingsPaths make_app_settings_paths(const CommandLineOptions& options) noexcept;

/// @brief Apply the parsed app name and CLI log level to loaded settings.
///
/// The parsed `app_name` is always copied, so an omitted option clears any
/// previously loaded name. The CLI log level wins over global, local and
/// environment sources; an absent level preserves the loaded one.
///
/// @param options Parsed options carrying the app name and optional override.
/// @param settings Merged settings updated in place; the log level is untouched
///        when no override was supplied.
/// @note Spec `app/command-line`, requirement "CLI log level overrides loaded
///       settings".
/// @note Spec `app/command-line`, requirement "App name option".
void apply_to(const CommandLineOptions& options, Settings& settings) noexcept;

/// @brief Map paths, load all four subsystems, then apply the CLI log level.
///
/// The log level is applied even when loading reports a diagnostic, so the
/// best-effort settings still honour the operator's override.
///
/// @param options Parsed options; both directories empty is rejected.
/// @param out Destination aggregate, populated best-effort by the loader.
/// @return `InvalidArgument` when both directories are empty (a manually built
///         options value cannot load built-in defaults); otherwise the
///         aggregate status of the four loaders.
/// @note Spec `app/command-line`, requirement "At least one settings directory
///       is required".
/// @note Cold path only: called before workers start.
[[nodiscard]] core::Status load_app_settings(const CommandLineOptions& options,
                                             Settings& out) noexcept;

/// @brief Static usage text naming every option and fixed file name.
///
/// The text lists `-gs/--global-settings`, `-ls/--local-settings`,
/// `-lgl/--log-level`, `-an/--app-name`, `-h/--help`, the accepted level
/// spellings and the four fixed JSON file names. The caller prints it; this
/// function never fails.
///
/// @return Static-storage view of the complete usage text.
/// @note Spec `app/command-line`, requirement "Help short-circuits with
///       success".
[[nodiscard]] std::string_view command_line_usage() noexcept;

} // namespace nexigen::app
