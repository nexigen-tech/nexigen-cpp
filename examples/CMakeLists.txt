# Runnable, self-contained examples for the public nexigen API.
#
# nexigen_add_example(name source target [extra_target...]): one executable
# per TU, linked against the module target(s) and Threads (jthread producers
# are part of the demonstrated API), and registered as a CTest smoke test. The
# 600 s timeout matches the unit-test convention so TSan/ASan runs do not
# flake. Extra targets are for cross-module examples (diagnostics + metrics).
function(nexigen_add_example name source target)
    include(Pch)
    # NO_TEST lets an example provide its own CTest invocation when the generic
    # "run with no arguments" smoke is not meaningful (for example, a runtime
    # that requires --app-id).
    set(_nexigen_example_args ${ARGN})
    list(FIND _nexigen_example_args "NO_TEST" _nexigen_no_test)
    if(_nexigen_no_test GREATER -1)
        list(REMOVE_AT _nexigen_example_args ${_nexigen_no_test})
    endif()
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${target} ${_nexigen_example_args} Threads::Threads)
    if(_nexigen_no_test EQUAL -1)
        add_test(NAME ${name} COMMAND ${name})
        # speedup-validation-loop 2.1 (design D4): each example test gets its
        # own working directory and its own NEXIGEN_RUN_DIR. Before this, all
        # examples of a module shared one cwd, and cwd-relative state
        # (./nexigen-example-settings at ex_app_cli_settings) raced under
        # ctest -j; unique per-test directories remove both collisions without
        # touching example sources.
        set(_nexigen_ex_run_dir "${CMAKE_BINARY_DIR}/examples/run/${name}")
        file(MAKE_DIRECTORY "${_nexigen_ex_run_dir}")
        set_tests_properties(${name} PROPERTIES
            TIMEOUT 600
            LABELS "examples"
            WORKING_DIRECTORY "${_nexigen_ex_run_dir}"
            ENVIRONMENT "NEXIGEN_RUN_DIR=${_nexigen_ex_run_dir}/nexigen-exec")
    endif()
    # 1.3: PCH ностикуется и примерам (тот же прелюд, тот же compiled state).
    nexigen_precompile(${name})
endfunction()

add_subdirectory(core)
add_subdirectory(chrono)
add_subdirectory(logging)
add_subdirectory(metrics)
add_subdirectory(tracing)
add_subdirectory(diagnostics)
add_subdirectory(app)
add_subdirectory(suite)
add_subdirectory(integration)
add_subdirectory(recorder)
