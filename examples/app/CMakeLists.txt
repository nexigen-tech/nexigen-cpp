# nexigen::app runnable examples.
nexigen_add_example(ex_app_lifecycle ex_app_lifecycle.cpp nexigen::app)
nexigen_add_example(ex_app_cli_settings ex_app_cli_settings.cpp nexigen::app)
nexigen_add_example(ex_logging_file_policy ex_logging_file_policy.cpp nexigen::app)
nexigen_add_example(ex_app_wait_loop ex_app_wait_loop.cpp nexigen::app)

if(NEXIGEN_ENABLE_COM)
    nexigen_add_example(ex_app_wait_loop_iceoryx2 ex_app_wait_loop_iceoryx2.cpp
        nexigen::app nexigen::logging_com nexigen::metrics_com nexigen::tracing_com nexigen::com)

    # Quickstart: App<Policy> with embedded time base, one input and one output.
    nexigen_add_example(ex_app_quickstart ex_app_quickstart.cpp
        nexigen::app nexigen::logging_com nexigen::metrics_com nexigen::tracing_com nexigen::com
        NO_TEST)
endif()

if(NEXIGEN_ENABLE_COM)
    # Application-side PHM participation (change add-app-phm-client): heartbeat
    # checkpoint + SafeState read from the SM Field in the run loop.
    nexigen_add_example(ex_app_phm_heartbeat ex_app_phm_heartbeat.cpp nexigen::app nexigen::phm)
endif()

# Argument-driven smoke variant: the --help path exits before the workload
# (spec examples "CTest smoke coverage for every example").
add_test(NAME ex_app_cli_settings_help COMMAND ex_app_cli_settings --help)
set_tests_properties(ex_app_cli_settings_help PROPERTIES TIMEOUT 600 LABELS "examples")
