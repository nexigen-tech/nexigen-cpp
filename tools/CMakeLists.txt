# nexigen-exec binary — links nexigen_exec + nexigen_app + nexigen_core.
add_executable(nexigen-exec nexigen-exec/main.cpp)
target_link_libraries(nexigen-exec PRIVATE nexigen_exec nexigen_app Threads::Threads)
target_include_directories(nexigen-exec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME nexigen_exec_help COMMAND nexigen-exec --help)
set_tests_properties(nexigen_exec_help PROPERTIES
    TIMEOUT 60 LABELS "tools")
# nexigen-hub — local observatory CLI.
add_executable(nexigen-hub nexigen-hub/main.cpp)
target_link_libraries(nexigen-hub PRIVATE nexigen_hub Threads::Threads)
target_include_directories(nexigen-hub PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME nexigen_hub_help COMMAND nexigen-hub --help)
set_tests_properties(nexigen_hub_help PROPERTIES
    TIMEOUT 60 LABELS "tools")

if(NEXIGEN_ENABLE_COM)
    # nexigen-phmd — Platform Health Management daemon (change add-phm).
    add_executable(nexigen-phmd nexigen-phmd/main.cpp)
    target_link_libraries(nexigen-phmd PRIVATE nexigen_phm Threads::Threads)
    target_include_directories(nexigen-phmd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME nexigen_phmd_help COMMAND nexigen-phmd --help)
    set_tests_properties(nexigen_phmd_help PROPERTIES
        TIMEOUT 60 LABELS "tools")

    add_test(NAME nexigen_phmd_hostile_config
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/hostile_config_test.sh
                $<TARGET_FILE:nexigen-phmd> --manifest)
    set_tests_properties(nexigen_phmd_hostile_config PROPERTIES
        TIMEOUT 60 LABELS "tools")

    # nexigen-sm — State Manager process (change add-sm).
    add_executable(nexigen-sm nexigen-sm/main.cpp)
    target_link_libraries(nexigen-sm PRIVATE nexigen_sm nexigen_phm Threads::Threads)
    target_include_directories(nexigen-sm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME nexigen_sm_help COMMAND nexigen-sm --help)
    set_tests_properties(nexigen_sm_help PROPERTIES
        TIMEOUT 60 LABELS "tools")

    add_test(NAME nexigen_sm_hostile_config
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/hostile_config_test.sh
                $<TARGET_FILE:nexigen-sm>)
    set_tests_properties(nexigen_sm_hostile_config PROPERTIES
        TIMEOUT 60 LABELS "tools")

    # nexigen-logd — Log Daemon (change add-log-daemon).
    add_executable(nexigen-logd nexigen-logd/main.cpp)
    target_link_libraries(nexigen-logd PRIVATE nexigen_logging nexigen_com Threads::Threads)
    target_include_directories(nexigen-logd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME nexigen_logd_help COMMAND nexigen-logd --help)
    set_tests_properties(nexigen_logd_help PROPERTIES
        TIMEOUT 60 LABELS "tools")

    add_test(NAME nexigen_logd_hostile_config
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/hostile_config_test.sh
                $<TARGET_FILE:nexigen-logd>)
    set_tests_properties(nexigen_logd_hostile_config PROPERTIES
        TIMEOUT 60 LABELS "tools")

    # Lifecycle: SIGTERM must take the graceful path (spec logging/log-daemon,
    # "Flush empties the buffer on shutdown").
    add_test(NAME nexigen_logd_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-logd/smoke_test.sh
                $<TARGET_FILE:nexigen-logd>)
    set_tests_properties(nexigen_logd_smoke PROPERTIES
        TIMEOUT 60 LABELS "tools")

    # nexigen-metricsd — Metrics Daemon (change add-metrics-daemon).
    add_executable(nexigen-metricsd nexigen-metricsd/main.cpp)
    target_link_libraries(nexigen-metricsd PRIVATE nexigen_metrics_com Threads::Threads)
    target_include_directories(nexigen-metricsd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME nexigen_metricsd_help COMMAND nexigen-metricsd --help)
    set_tests_properties(nexigen_metricsd_help PROPERTIES
        TIMEOUT 60 LABELS "tools")

    add_test(NAME nexigen_metricsd_hostile_config
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/hostile_config_test.sh
                $<TARGET_FILE:nexigen-metricsd>)
    set_tests_properties(nexigen_metricsd_hostile_config PROPERTIES
        TIMEOUT 60 LABELS "tools")

    add_test(NAME nexigen_metricsd_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-metricsd/smoke_test.sh
                $<TARGET_FILE:nexigen-metricsd>)
    set_tests_properties(nexigen_metricsd_smoke PROPERTIES
        TIMEOUT 60 LABELS "tools")

    # nexigen-traced — Trace Daemon (change add-trace-daemon).
    add_executable(nexigen-traced nexigen-traced/main.cpp)
    target_link_libraries(nexigen-traced PRIVATE nexigen_tracing_com nexigen_chrono Threads::Threads)
    target_include_directories(nexigen-traced PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME nexigen_traced_help COMMAND nexigen-traced --help)
    set_tests_properties(nexigen_traced_help PROPERTIES
        TIMEOUT 60 LABELS "tools")

    add_test(NAME nexigen_traced_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-traced/smoke_test.sh
                $<TARGET_FILE:nexigen-traced>)
    set_tests_properties(nexigen_traced_smoke PROPERTIES
        TIMEOUT 60 LABELS "tools")

    install(TARGETS nexigen-phmd nexigen-sm nexigen-traced
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

install(TARGETS nexigen-exec nexigen-hub
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
