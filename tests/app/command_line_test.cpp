// Split from app_test.cpp: nexigen::app command-line parser, mapping and loading.

#include "app_test_support.hpp"

namespace {

void test_command_line_parser_long_options() {
    // Long options: separate and '=' value forms.
    {
        auto parsed =
            parse_args({"--global-settings", "/etc/nexigen", "--local-settings", "./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/nexigen");
            NX_CHECK(parsed->local_settings_dir == "./hil-config");
            NX_CHECK(!parsed->log_level.has_value());
            NX_CHECK(!parsed->help);
        }
    }
    {
        auto parsed =
            parse_args({"--global-settings=/etc/nexigen", "--local-settings=./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/nexigen");
            NX_CHECK(parsed->local_settings_dir == "./hil-config");
        }
    }
}

void test_command_line_parser_short_forms() {
    // Multi-character short names: separate, adjacent and '=' value forms.
    {
        auto parsed = parse_args({"-gs", "/etc/nexigen", "-ls", "./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/nexigen");
            NX_CHECK(parsed->local_settings_dir == "./hil-config");
        }
    }
    {
        auto parsed = parse_args({"-gs/etc/nexigen", "-ls./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/nexigen");
            NX_CHECK(parsed->local_settings_dir == "./hil-config");
        }
    }
    {
        auto parsed = parse_args({"-gs=/etc/nexigen", "-ls=./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/nexigen");
            NX_CHECK(parsed->local_settings_dir == "./hil-config");
        }
    }
}

void test_command_line_parser_levels() {
    // Log-level short forms.
    for (const std::string_view spelling : {"-lgl info", "-lglinfo", "-lgl=info"}) {
        const std::size_t split = spelling.find(' ');
        const std::string_view token = spelling.substr(0, split);
        auto parsed = split == std::string_view::npos
                          ? parse_args({"-gs", "/etc/nexigen", token})
                          : parse_args({"-gs", "/etc/nexigen", token, spelling.substr(split + 1)});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->log_level.has_value());
            NX_CHECK(parsed->log_level == core::LogLevel::Info);
        }
    }

    // Every canonical level spelling.
    struct LevelCase {
        std::string_view name;
        core::LogLevel level;
    };
    constexpr LevelCase kLevels[] = {
        {"verbose", core::LogLevel::Verbose}, {"debug", core::LogLevel::Debug},
        {"info", core::LogLevel::Info},       {"warning", core::LogLevel::Warning},
        {"error", core::LogLevel::Error},     {"fatal", core::LogLevel::Fatal},
        {"off", core::LogLevel::Off},
    };
    for (const LevelCase& level : kLevels) {
        auto parsed = parse_args({"-gs", "/etc/nexigen", "-lgl", level.name});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->log_level.has_value());
            NX_CHECK(parsed->log_level == level.level);
        }
    }

    // Unknown and mixed-case levels are rejected with the accepted list.
    for (const std::string_view bad_level : {"chatty", "INFO", ""}) {
        auto parsed = parse_args({"-gs", "/etc/nexigen", "--log-level", bad_level});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--log-level"));
            NX_CHECK(message_contains(parsed.error(), "verbose"));
            NX_CHECK(message_contains(parsed.error(), "off"));
        }
    }
}

void test_command_line_parser_aliases() {
    // Single-letter aliases are not part of the contract.
    for (const std::string_view alias : {"-g", "-l", "-L"}) {
        auto parsed = parse_args({alias, "/etc/nexigen"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), alias));
        }
    }
}

void test_command_line_parser_argv0() {
    // argv[0] is the program name and is skipped.
    {
        const char* const argv[] = {"my-app", "--help"};
        auto parsed = app::parse_command_line(2, argv);
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->help);
        }
        const char* const dirs[] = {"my-app", "-gs", "/etc/nexigen"};
        auto from_argv = app::parse_command_line(3, dirs);
        NX_CHECK(from_argv.has_value());
        if (from_argv.has_value()) {
            NX_CHECK(from_argv->global_settings_dir == "/etc/nexigen");
        }
        auto no_arguments = app::parse_command_line(1, argv);
        NX_CHECK(!no_arguments.has_value());
        auto null_argv = app::parse_command_line(0, nullptr);
        NX_CHECK(!null_argv.has_value());
    }
}

void test_command_line_parser_help() {
    // Help wins over every other argument and never requires a directory.
    for (const std::string_view help_token : {"-h", "--help"}) {
        auto parsed = parse_args({help_token});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->help);
        }
    }
    {
        auto parsed = parse_args({"--colour", "--global-settings", "./relative", "--help"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->help);
        }
    }
}

void test_command_line_parser_terminator() {
    // Terminator: following tokens are positional; a bare terminator is fine.
    {
        auto parsed = parse_args({"--local-settings", "./config", "--"});
        NX_CHECK(parsed.has_value());
    }
    {
        auto parsed = parse_args({"--local-settings", "./config", "--", "extra"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "extra"));
        }
    }
    {
        auto parsed = parse_args({"--local-settings", "./config", "--", "--help"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--help"));
        }
    }
}

void test_command_line_parser_errors() {
    // Unknown, missing, repeated and positional arguments.
    {
        auto parsed = parse_args({"--colour"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--colour"));
        }
    }
    {
        auto parsed = parse_args({"--global-settings"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--global-settings"));
        }
    }
    {
        auto parsed = parse_args({"--global-settings", "--local-settings", "./config"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--global-settings"));
        }
    }
    {
        auto parsed = parse_args(
            {"-gs", "/etc/a", "-gs", "/etc/b", "-ls", "./x", "-lgl", "debug", "-lgl", "error"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir == "/etc/b");
            NX_CHECK(parsed->log_level == core::LogLevel::Error);
        }
    }
    {
        auto parsed = parse_args({"config"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "config"));
        }
    }
    {
        auto parsed = parse_args({"-"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "-"));
        }
    }
}

void test_command_line_parser_directory_validation() {
    // Directory shape validation and empty values.
    {
        auto parsed = parse_args({"--global-settings", "./config"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--global-settings"));
        }
    }
    {
        auto parsed = parse_args({"--local-settings", "/etc/nexigen"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--local-settings"));
        }
    }
    {
        auto parsed = parse_args({"--global-settings=", "--local-settings", "./x"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--global-settings"));
        }
    }
    {
        auto parsed = parse_args({"--global-settings", "", "--local-settings", "./x"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--global-settings"));
        }
    }
    {
        auto parsed = parse_args({"--local-settings=", "--global-settings", "/etc/nexigen"});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--local-settings"));
        }
    }
}

void test_command_line_parser_required_directory() {
    // At least one directory is required, except for help.
    {
        auto parsed = parse_args({});
        NX_CHECK(!parsed.has_value());
        if (!parsed.has_value()) {
            NX_CHECK(has_diagnostic(parsed.error(), "--global-settings/--local-settings"));
            NX_CHECK(
                message_contains(parsed.error(), "at least one settings directory is required"));
        }
    }
    {
        auto parsed = parse_args({"--global-settings", "/etc/nexigen"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->local_settings_dir.empty());
        }
    }
    {
        auto parsed = parse_args({"--local-settings", "./hil-config"});
        NX_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            NX_CHECK(parsed->global_settings_dir.empty());
        }
    }
}

void test_command_line_parser_usage() {
    // Usage text names the whole contract.
    const std::string_view usage = app::command_line_usage();
    for (const std::string_view token :
         {"-gs", "--global-settings", "-ls", "--local-settings", "-lgl", "--log-level", "-h",
          "--help", "verbose", "off", "logging.json", "metrics.json", "tracing.json",
          "chrono.json"}) {
        NX_CHECK(usage.find(token) != std::string_view::npos);
    }
}

} // namespace

void test_command_line_parser() {
    test_command_line_parser_long_options();
    test_command_line_parser_short_forms();
    test_command_line_parser_levels();
    test_command_line_parser_aliases();
    test_command_line_parser_argv0();
    test_command_line_parser_help();
    test_command_line_parser_terminator();
    test_command_line_parser_errors();
    test_command_line_parser_directory_validation();
    test_command_line_parser_required_directory();
    test_command_line_parser_usage();
}

void test_command_line_mapping() {
    app::CommandLineOptions options{};
    options.global_settings_dir = "/etc/nexigen";
    options.local_settings_dir = "./hil-config";
    app::AppSettingsPaths paths = app::make_app_settings_paths(options);
    NX_CHECK(paths.logging.global_file == "/etc/nexigen/logging.json");
    NX_CHECK(paths.logging.local_file == "./hil-config/logging.json");
    NX_CHECK(paths.metrics.global_file == "/etc/nexigen/metrics.json");
    NX_CHECK(paths.metrics.local_file == "./hil-config/metrics.json");
    NX_CHECK(paths.tracing.global_file == "/etc/nexigen/tracing.json");
    NX_CHECK(paths.tracing.local_file == "./hil-config/tracing.json");
    NX_CHECK(paths.chrono.global_file == "/etc/nexigen/chrono.json");
    NX_CHECK(paths.chrono.local_file == "./hil-config/chrono.json");

    options.global_settings_dir = "/etc/nexigen/";
    paths = app::make_app_settings_paths(options);
    NX_CHECK(paths.logging.global_file == "/etc/nexigen/logging.json");
    NX_CHECK(paths.metrics.global_file == "/etc/nexigen/metrics.json");
    NX_CHECK(paths.tracing.global_file == "/etc/nexigen/tracing.json");
    NX_CHECK(paths.chrono.global_file == "/etc/nexigen/chrono.json");
    NX_CHECK(paths.logging.global_file.find("//") == std::string::npos);

    options.global_settings_dir.clear();
    paths = app::make_app_settings_paths(options);
    NX_CHECK(paths.logging.global_file.empty());
    NX_CHECK(paths.metrics.global_file.empty());
    NX_CHECK(paths.tracing.global_file.empty());
    NX_CHECK(paths.chrono.global_file.empty());
    NX_CHECK(paths.logging.local_file == "./hil-config/logging.json");

    app::Settings settings{};
    settings.logging.options.initial_log_level = core::LogLevel::Debug;
    options.log_level = core::LogLevel::Warning;
    app::apply_to(options, settings);
    NX_CHECK(settings.logging.options.initial_log_level == core::LogLevel::Warning);

    options.log_level.reset();
    settings.logging.options.initial_log_level = core::LogLevel::Error;
    app::apply_to(options, settings);
    NX_CHECK(settings.logging.options.initial_log_level == core::LogLevel::Error);
}

void test_command_line_serialization() {
    const app::CommandLineDiagnostic diagnostic{"--log-level", "bad level"};
    NX_CHECK(app::to_string(diagnostic) == diagnostic.to_string());
    NX_CHECK(std::format("{}", diagnostic) == diagnostic.to_string());
    NX_CHECK(diagnostic.to_string().find("--log-level") != std::string::npos);

    app::CommandLineOptions options{};
    options.global_settings_dir = "/etc/nexigen";
    options.local_settings_dir = "./hil-config";
    options.log_level = core::LogLevel::Warning;
    options.help = true;
    NX_CHECK(app::to_string(options) == options.to_string());
    NX_CHECK(std::format("{}", options) == options.to_string());
    NX_CHECK(options.to_string().find("warning") != std::string::npos);

    const app::CommandLineError error{core::ErrorCode::InvalidArgument, {diagnostic}};
    NX_CHECK(app::to_string(error) == error.to_string());
    NX_CHECK(std::format("{}", error) == error.to_string());
    NX_CHECK(error.to_string().find("InvalidArgument") != std::string::npos);

    nlohmann::json diagnostic_json;
    app::to_json(diagnostic_json, diagnostic);
    NX_CHECK(diagnostic_json["option"] == "--log-level");
    NX_CHECK(diagnostic_json["message"] == "bad level");
    app::CommandLineDiagnostic recovered_diagnostic{};
    app::from_json(diagnostic_json, recovered_diagnostic);
    NX_CHECK(recovered_diagnostic.option == diagnostic.option);
    NX_CHECK(recovered_diagnostic.message == diagnostic.message);

    app::CommandLineDiagnostic unchanged_diagnostic{"keep", "keep"};
    app::from_json(nlohmann::json::array(), unchanged_diagnostic);
    NX_CHECK(unchanged_diagnostic.option == "keep");
    app::from_json(nlohmann::json{{"option", 7}}, unchanged_diagnostic);
    NX_CHECK(unchanged_diagnostic.option == "keep");
    app::from_json(nlohmann::json{{"message", "new"}}, unchanged_diagnostic);
    NX_CHECK(unchanged_diagnostic.option.empty());
    NX_CHECK(unchanged_diagnostic.message == "new");

    app::CommandLineDiagnostic defaulted_diagnostic{"keep", "keep"};
    app::from_json(nlohmann::json::object(), defaulted_diagnostic);
    NX_CHECK(defaulted_diagnostic.option.empty());
    NX_CHECK(defaulted_diagnostic.message.empty());

    nlohmann::json options_json;
    app::to_json(options_json, options);
    NX_CHECK(options_json["global_settings_dir"] == "/etc/nexigen");
    NX_CHECK(options_json["local_settings_dir"] == "./hil-config");
    NX_CHECK(options_json["log_level"] == "warning");
    NX_CHECK(options_json["help"] == true);
    app::CommandLineOptions recovered_options{};
    app::from_json(options_json, recovered_options);
    NX_CHECK(recovered_options.global_settings_dir == "/etc/nexigen");
    NX_CHECK(recovered_options.local_settings_dir == "./hil-config");
    NX_CHECK(recovered_options.log_level == core::LogLevel::Warning);
    NX_CHECK(recovered_options.help);

    app::CommandLineOptions defaulted_options{};
    defaulted_options.global_settings_dir = "/keep";
    defaulted_options.local_settings_dir = "./keep";
    defaulted_options.log_level = core::LogLevel::Warning;
    defaulted_options.help = true;
    app::from_json(nlohmann::json::object(), defaulted_options);
    NX_CHECK(defaulted_options.global_settings_dir.empty());
    NX_CHECK(defaulted_options.local_settings_dir.empty());
    NX_CHECK(defaulted_options.log_level == core::LogLevel::Info);
    NX_CHECK(!defaulted_options.help);

    app::CommandLineOptions partial_options{};
    app::from_json(nlohmann::json{{"global_settings_dir", "/only"}}, partial_options);
    NX_CHECK(partial_options.global_settings_dir == "/only");
    NX_CHECK(partial_options.local_settings_dir.empty());
    NX_CHECK(partial_options.log_level == core::LogLevel::Info);
    NX_CHECK(!partial_options.help);

    app::CommandLineOptions kept_options{};
    kept_options.global_settings_dir = "/keep";
    kept_options.log_level = core::LogLevel::Warning;
    app::from_json(nlohmann::json{{"help", "yes"}}, kept_options);
    NX_CHECK(kept_options.global_settings_dir == "/keep");
    NX_CHECK(kept_options.log_level == core::LogLevel::Warning);
    app::from_json(nlohmann::json{{"log_level", "chatty"}}, kept_options);
    NX_CHECK(kept_options.global_settings_dir == "/keep");
    NX_CHECK(kept_options.log_level == core::LogLevel::Warning);

    app::CommandLineOptions non_object_options{};
    non_object_options.global_settings_dir = "/keep";
    non_object_options.log_level = core::LogLevel::Warning;
    app::from_json(nlohmann::json::array(), non_object_options);
    NX_CHECK(non_object_options.global_settings_dir == "/keep");
    NX_CHECK(non_object_options.log_level == core::LogLevel::Warning);

    options.log_level.reset();
    app::to_json(options_json, options);
    NX_CHECK(options_json["log_level"].is_null());
    app::from_json(options_json, recovered_options);
    NX_CHECK(!recovered_options.log_level.has_value());

    nlohmann::json error_json;
    app::to_json(error_json, error);
    NX_CHECK(error_json["code"] == "InvalidArgument");
    NX_CHECK(error_json["diagnostics"].size() == 1U);
    app::CommandLineError recovered_error{};
    app::from_json(error_json, recovered_error);
    NX_CHECK(recovered_error.code == core::ErrorCode::InvalidArgument);
    NX_CHECK(recovered_error.diagnostics.size() == 1U);
    NX_CHECK(recovered_error.diagnostics[0].option == "--log-level");
    NX_CHECK(recovered_error.diagnostics[0].message == "bad level");

    app::CommandLineError unchanged_error{core::ErrorCode::Conflict, {diagnostic}};
    app::from_json(nlohmann::json(7), unchanged_error);
    NX_CHECK(unchanged_error.code == core::ErrorCode::Conflict);
    NX_CHECK(unchanged_error.diagnostics.size() == 1U);
    app::from_json(nlohmann::json{{"code", 7}}, unchanged_error);
    NX_CHECK(unchanged_error.code == core::ErrorCode::Conflict);
    app::from_json(nlohmann::json{{"diagnostics", "nope"}}, unchanged_error);
    NX_CHECK(unchanged_error.diagnostics.size() == 1U);
    app::from_json(nlohmann::json{{"diagnostics", nlohmann::json::array({7})}}, unchanged_error);
    NX_CHECK(unchanged_error.diagnostics.size() == 1U);
    app::from_json(nlohmann::json{{"diagnostics", nlohmann::json::array({{{"option", 7}}})}},
                   unchanged_error);
    NX_CHECK(unchanged_error.diagnostics.size() == 1U);
    NX_CHECK(unchanged_error.diagnostics[0].option == "--log-level");

    app::CommandLineError defaulted_error{core::ErrorCode::Conflict, {diagnostic}};
    app::from_json(nlohmann::json::object(), defaulted_error);
    NX_CHECK(defaulted_error.code == core::ErrorCode::InvalidArgument);
    NX_CHECK(defaulted_error.diagnostics.empty());

    app::CommandLineError partial_error{core::ErrorCode::Conflict, {diagnostic}};
    app::from_json(nlohmann::json{{"diagnostics", nlohmann::json::array({{{"message", "m"}}})}},
                   partial_error);
    NX_CHECK(partial_error.code == core::ErrorCode::InvalidArgument);
    NX_CHECK(partial_error.diagnostics.size() == 1U);
    NX_CHECK(partial_error.diagnostics[0].option.empty());
    NX_CHECK(partial_error.diagnostics[0].message == "m");
}

void test_command_line_loading() {
    EnvGuard log_level_env{"NEXIGEN_LOG_LEVEL"};
    log_level_env.clear();

    const std::string global_dir = unique_path("app_cli_global");
    const std::string local_dir = unique_path("app_cli_local");
    make_dir(global_dir);
    make_dir(local_dir);
    write_text(global_dir + "/logging.json", R"({"initial_log_level":"debug"})");
    write_text(local_dir + "/logging.json", R"({"initial_log_level":"error"})");

    app::CommandLineOptions options{};
    options.global_settings_dir = global_dir;
    options.log_level = core::LogLevel::Warning;
    app::Settings loaded{};
    core::Status status = app::load_app_settings(options, loaded);
    NX_CHECK(status.has_value());
    NX_CHECK(loaded.logging.global_loaded);
    NX_CHECK(!loaded.logging.local_loaded);
    NX_CHECK(loaded.logging.options.initial_log_level == core::LogLevel::Warning);

    options.local_settings_dir = local_dir;
    status = app::load_app_settings(options, loaded);
    NX_CHECK(status.has_value());
    NX_CHECK(loaded.logging.global_loaded);
    NX_CHECK(loaded.logging.local_loaded);
    NX_CHECK(loaded.logging.options.initial_log_level == core::LogLevel::Warning);

    // A bad file is best-effort: the CLI level still wins and diagnostics stay.
    write_text(global_dir + "/logging.json", "{");
    app::Settings failed{};
    status = app::load_app_settings(options, failed);
    NX_CHECK(!status.has_value());
    if (!status.has_value()) {
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
    }
    NX_CHECK(!failed.logging.diagnostics.empty());
    NX_CHECK(failed.logging.options.initial_log_level == core::LogLevel::Warning);

    // Manually built options with no directory cannot load built-in defaults.
    app::CommandLineOptions empty{};
    app::Settings untouched{};
    status = app::load_app_settings(empty, untouched);
    NX_CHECK(!status.has_value());
    if (!status.has_value()) {
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
    }

    // Local-only options leave the global source disabled.
    app::CommandLineOptions local_only{};
    local_only.local_settings_dir = local_dir;
    app::Settings local_loaded{};
    status = app::load_app_settings(local_only, local_loaded);
    NX_CHECK(status.has_value());
    NX_CHECK(!local_loaded.logging.global_loaded);
    NX_CHECK(local_loaded.logging.local_loaded);

    static_cast<void>(std::remove((global_dir + "/logging.json").c_str()));
    static_cast<void>(std::remove((local_dir + "/logging.json").c_str()));
    static_cast<void>(::rmdir(global_dir.c_str()));
    static_cast<void>(::rmdir(local_dir.c_str()));
}

void test_command_line_infrastructure() {
    EnvGuard log_level_env{"NEXIGEN_LOG_LEVEL"};
    log_level_env.clear();

    const std::string dir = unique_path("app_cli_infra");
    make_dir(dir);
    write_text(dir + "/logging.json", R"({"initial_log_level":"debug"})");

    app::CommandLineOptions options{};
    options.global_settings_dir = dir;
    options.log_level = core::LogLevel::Warning;

    auto infra = make_test_infra<TestPolicy>();
    NX_CHECK(infra != nullptr);
    if (infra) {
        app::Settings loaded{};
        const core::Status status = infra->initialize(options, loaded);
        NX_CHECK(status.has_value());
        NX_CHECK(loaded.logging.global_loaded);
        NX_CHECK(loaded.logging.options.initial_log_level == core::LogLevel::Warning);
        NX_CHECK(infra->meter("sim", "1.0").has_value());
        NX_CHECK(infra->shutdown().has_value());
    }

    // Fail fast on a loader error; loaded keeps best-effort data and a later
    // explicit initialize(loaded) opts into degraded startup.
    write_text(dir + "/logging.json", "{");
    auto failing = make_test_infra<TestPolicy>();
    NX_CHECK(failing != nullptr);
    if (failing) {
        app::Settings failed{};
        const core::Status status = failing->initialize(options, failed);
        NX_CHECK(!status.has_value());
        if (!status.has_value()) {
            NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
        }
        NX_CHECK(!failing->meter("x", "1").has_value());
        NX_CHECK(!failed.logging.diagnostics.empty());
        NX_CHECK(failed.logging.options.initial_log_level == core::LogLevel::Warning);
        NX_CHECK(failing->initialize(failed).has_value());
        NX_CHECK(failing->meter("x", "1").has_value());
        NX_CHECK(failing->shutdown().has_value());
    }

    // Help is a caller action, not an initialization request.
    auto help_infra = make_test_infra<TestPolicy>();
    NX_CHECK(help_infra != nullptr);
    if (help_infra) {
        app::CommandLineOptions help_options{};
        help_options.help = true;
        app::Settings help_loaded{};
        const core::Status status = help_infra->initialize(help_options, help_loaded);
        NX_CHECK(!status.has_value());
        if (!status.has_value()) {
            NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
        }
        NX_CHECK(!help_infra->meter("x", "1").has_value());
        NX_CHECK(help_infra->shutdown().has_value());
    }

    // A manually built options value with no directory is rejected too.
    auto empty_infra = make_test_infra<TestPolicy>();
    NX_CHECK(empty_infra != nullptr);
    if (empty_infra) {
        app::CommandLineOptions empty{};
        app::Settings empty_loaded{};
        const core::Status status = empty_infra->initialize(empty, empty_loaded);
        NX_CHECK(!status.has_value());
        if (!status.has_value()) {
            NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
        }
        NX_CHECK(!empty_infra->meter("x", "1").has_value());
        NX_CHECK(empty_infra->shutdown().has_value());
    }

    // The pre-existing settings overload remains unchanged.
    auto direct = make_test_infra<TestPolicy>();
    NX_CHECK(direct != nullptr);
    if (direct) {
        NX_CHECK(direct->initialize(app::Settings{}).has_value());
        NX_CHECK(direct->shutdown().has_value());
    }

    static_cast<void>(std::remove((dir + "/logging.json").c_str()));
    static_cast<void>(::rmdir(dir.c_str()));
}

void run_app_command_line_suites() {
    run_suite("command_line", test_command_line_parser);
    run_suite("cli_mapping", test_command_line_mapping);
    run_suite("cli_serialization", test_command_line_serialization);
    run_suite("cli_loading", test_command_line_loading);
    run_suite("cli_infrastructure", test_command_line_infrastructure);
}
