# nexigen-exec binary — links nexigen_exec + nexigen_app + nexigen_core.
add_executable(nexigen-exec nexigen-exec/main.cpp)
target_link_libraries(nexigen-exec PRIVATE nexigen_exec nexigen_app Threads::Threads)
target_include_directories(nexigen-exec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(NEXIGEN_ENABLE_COM)
    target_link_libraries(nexigen-exec PRIVATE nexigen_exec_control)
    target_compile_definitions(nexigen-exec PRIVATE NEXIGEN_ENABLE_COM=1)
endif()

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

    # nexigen-em — Execution Manager suite front-end (changes
    # introduce-platformfg-process-model, unify-exec-suite-control-path): starts
    # the detached suite supervisor, which hosts the StateClient and feeds the
    # readiness tracker from the per-application state channels.
    add_executable(nexigen-em nexigen-em/main.cpp)
    target_link_libraries(nexigen-em PRIVATE nexigen_exec_control nexigen_app Threads::Threads)
    target_compile_definitions(nexigen-em PRIVATE NEXIGEN_ENABLE_COM=1)
    target_include_directories(nexigen-em PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME nexigen_em_help COMMAND nexigen-em --help)
    set_tests_properties(nexigen_em_help PROPERTIES
        TIMEOUT 60 LABELS "tools")

    # End-to-end suite smoke: nexigen-em starts a detached supervisor, Run is
    # sent to the standard command channel until the one-shot completes, and the
    # supervisor is stopped through its control file.
    add_executable(nexigen_em_smoke_tests nexigen-em/smoke_test.cpp)
    target_link_libraries(nexigen_em_smoke_tests PRIVATE
        nexigen_exec nexigen_com nexigen_app Threads::Threads)
    target_include_directories(nexigen_em_smoke_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME nexigen_em_smoke
        COMMAND nexigen_em_smoke_tests $<TARGET_FILE:nexigen-em>)
    set_tests_properties(nexigen_em_smoke PROPERTIES TIMEOUT 60 LABELS "tools")

    add_test(NAME nexigen_phmd_required_spec
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-phmd/required_spec_test.sh
                $<TARGET_FILE:nexigen-phmd>)
    set_tests_properties(nexigen_phmd_required_spec PROPERTIES
        TIMEOUT 60 LABELS "tools")

    add_test(NAME nexigen_phmd_safety_sections
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-phmd/safety_sections_test.sh
                $<TARGET_FILE:nexigen-phmd> ${CMAKE_SOURCE_DIR})
    set_tests_properties(nexigen_phmd_safety_sections PROPERTIES
        TIMEOUT 60 LABELS "tools")

    # Identity e2e (change checkpoint-identity): gating, security events, and
    # the per-incarnation reset. The helper claims its own pid/uid; the script
    # skips as root because expected_uid rejects 0.
    add_executable(nexigen-phmd-identity-helper ${CMAKE_SOURCE_DIR}/tests/tools/phmd_identity_helper.cpp)
    target_link_libraries(nexigen-phmd-identity-helper PRIVATE nexigen_phm Threads::Threads)
    target_include_directories(nexigen-phmd-identity-helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME nexigen_phmd_identity
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-phmd/identity_test.sh
                $<TARGET_FILE:nexigen-phmd> $<TARGET_FILE:nexigen-phmd-identity-helper>)
    set_tests_properties(nexigen_phmd_identity PROPERTIES
        TIMEOUT 120 LABELS "tools")

    # Safety-loop e2e (change complete-phm-safety-loop): the helper plays the
    # application, the Execution Manager, and the State Manager so the full
    # Expired -> restart -> Failed -> escalate -> ack ladder runs against the
    # real daemon.
    add_executable(nexigen-phmd-safety-loop-helper
        ${CMAKE_SOURCE_DIR}/tests/tools/phmd_safety_loop_helper.cpp)
    target_link_libraries(nexigen-phmd-safety-loop-helper PRIVATE nexigen_phm Threads::Threads)
    target_include_directories(nexigen-phmd-safety-loop-helper
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME nexigen_phmd_safety_loop
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-phmd/safety_loop_test.sh
                $<TARGET_FILE:nexigen-phmd> $<TARGET_FILE:nexigen-phmd-safety-loop-helper>)
    set_tests_properties(nexigen_phmd_safety_loop PROPERTIES
        TIMEOUT 180 LABELS "tools")

    add_test(NAME nexigen_sm_required_spec
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-sm/required_spec_test.sh
                $<TARGET_FILE:nexigen-sm>)
    set_tests_properties(nexigen_sm_required_spec PROPERTIES
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

    add_test(NAME nexigen_logd_required_spec
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-logd/required_spec_test.sh
                $<TARGET_FILE:nexigen-logd>)
    set_tests_properties(nexigen_logd_required_spec PROPERTIES
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

    install(TARGETS nexigen-phmd nexigen-sm nexigen-em nexigen-traced
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

install(TARGETS nexigen-exec nexigen-hub
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
