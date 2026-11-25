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
}

} // namespace

namespace canview_test {

void run_cli_tests() {
    test_identity();
    test_cli();
}

} // namespace canview_test
