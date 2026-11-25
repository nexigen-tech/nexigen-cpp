// Command-line parsing tests for the nexigen-canview scaffold.
#include <string_view>

#include "cli.hpp"
#include "suite.hpp"
#include "support/check.hpp"
#include "version.hpp"

namespace {

namespace canview = nexigen::canview;

void test_identity() {
    NX_CHECK(canview::kToolName == "nexigen-canview");
    NX_CHECK(!canview::version().empty());
}

void test_cli() {
    char arg0[] = "nexigen-canview";
    char arg_help[] = "--help";
    char* help_argv[] = {arg0, arg_help};
    const auto help = canview::parse_cli(2, help_argv);
    NX_CHECK(help.has_value());
    NX_CHECK(help->help);
    NX_CHECK(!help->version);

    char arg_version[] = "--version";
    char* version_argv[] = {arg0, arg_version};
    const auto version = canview::parse_cli(2, version_argv);
    NX_CHECK(version.has_value());
    NX_CHECK(version->version);

    char arg_bad[] = "--no-such-option";
    char* bad_argv[] = {arg0, arg_bad};
    const auto bad = canview::parse_cli(2, bad_argv);
    NX_CHECK(!bad.has_value());
    NX_CHECK(bad.error() == nexigen::core::ErrorCode::InvalidArgument);

    NX_CHECK(!canview::usage_text().empty());

    char arg_fps[] = "--ui-fps";
    char value_fps[] = "5";
    char* fps_argv[] = {arg0, arg_fps, value_fps};
    const auto fps = canview::parse_cli(3, fps_argv);
    NX_CHECK(fps.has_value());
    NX_CHECK(fps->ui_fps == 5U);

    char value_bad_fps[] = "4";
    char* bad_fps_argv[] = {arg0, arg_fps, value_bad_fps};
    NX_CHECK(!canview::parse_cli(3, bad_fps_argv).has_value());
}

} // namespace

namespace canview_test {

void run_cli_tests() {
    test_identity();
    test_cli();
}

} // namespace canview_test
