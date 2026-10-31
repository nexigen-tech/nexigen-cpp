# nexigen-run binary — links nexigen_runner + nexigen_app + nexigen_core.
add_executable(nexigen-run nexigen-run/main.cpp)
target_link_libraries(nexigen-run PRIVATE nexigen_runner nexigen_app Threads::Threads)

add_test(NAME nexigen_run_help COMMAND nexigen-run --help)
set_tests_properties(nexigen_run_help PROPERTIES
    TIMEOUT 60 LABELS "tools")