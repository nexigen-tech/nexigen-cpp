# nexigen::app runnable examples.
nexigen_add_example(ex_app_lifecycle ex_app_lifecycle.cpp nexigen::app)
nexigen_add_example(ex_app_cli_settings ex_app_cli_settings.cpp nexigen::app)

# Argument-driven smoke variant: the --help path exits before the workload
# (spec examples "CTest smoke coverage for every example").
add_test(NAME ex_app_cli_settings_help COMMAND ex_app_cli_settings --help)
set_tests_properties(ex_app_cli_settings_help PROPERTIES TIMEOUT 600 LABELS "examples")
