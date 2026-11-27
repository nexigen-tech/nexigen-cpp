if(NEXIGEN_BUILD_LOGVIEW)
    add_subdirectory(nexigen-logview)
endif()

if(NEXIGEN_BUILD_CANVIEW)
    add_subdirectory(nexigen-canview)
endif()

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

# nexigen-chronod — time-base daemon (change add-time-base-service). Ungated:
# the time base must build and run with NEXIGEN_ENABLE_COM=OFF.
add_executable(nexigen-chronod nexigen-chronod/main.cpp)
target_link_libraries(nexigen-chronod PRIVATE nexigen_chrono nexigen_recorder Threads::Threads)
target_include_directories(nexigen-chronod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Test-only probe that attaches to a live chronod like an application.
add_executable(nexigen_chronod_live_probe nexigen-chronod/live_probe.cpp)
target_link_libraries(nexigen_chronod_live_probe PRIVATE nexigen_chrono Threads::Threads)

add_test(NAME nexigen_chronod_help COMMAND nexigen-chronod --help)
set_tests_properties(nexigen_chronod_help PROPERTIES
    TIMEOUT 60 LABELS "tools")

add_test(NAME nexigen_chronod_smoke
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-chronod/smoke_test.sh
            $<TARGET_FILE:nexigen-chronod>
            $<TARGET_FILE:nexigen_chronod_live_probe>
            $<TARGET_FILE:nexigen-export>)
set_tests_properties(nexigen_chronod_smoke PROPERTIES
    TIMEOUT 60 LABELS "tools")

if(NEXIGEN_ENABLE_COM)
    # nexigen-phmd — Platform Health Management daemon (change add-phm).
    add_executable(nexigen-phmd nexigen-phmd/main.cpp)
    target_link_libraries(nexigen-phmd PRIVATE nexigen_phm Threads::Threads)
    target_include_directories(nexigen-phmd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME nexigen_phmd_help COMMAND nexigen-phmd --help)
    set_tests_properties(nexigen_phmd_help PROPERTIES
        TIMEOUT 60 LABELS "tools")

    add_test(NAME nexigen_phmd_usage
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-phmd/usage_test.sh
                $<TARGET_FILE:nexigen-phmd>)
    set_tests_properties(nexigen_phmd_usage PROPERTIES
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
    set_tests_properties(nexigen_em_smoke PROPERTIES TIMEOUT 60 LABELS "tools" RESOURCE_LOCK "nexigen-heavy-com" RUN_SERIAL TRUE)

    add_test(NAME nexigen_phmd_required_spec
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-phmd/required_spec_test.sh
                $<TARGET_FILE:nexigen-phmd>)
    set_tests_properties(nexigen_phmd_required_spec PROPERTIES
        TIMEOUT 60 LABELS "tools" RESOURCE_LOCK "nexigen-heavy-com" RUN_SERIAL TRUE)

    add_test(NAME nexigen_phmd_safety_sections
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-phmd/safety_sections_test.sh
                $<TARGET_FILE:nexigen-phmd> ${CMAKE_SOURCE_DIR})
    set_tests_properties(nexigen_phmd_safety_sections PROPERTIES
        TIMEOUT 60 LABELS "tools" RESOURCE_LOCK "nexigen-heavy-com" RUN_SERIAL TRUE)

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
        TIMEOUT 120 LABELS "tools" RESOURCE_LOCK "nexigen-heavy-com" RUN_SERIAL TRUE)

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
        TIMEOUT 180 LABELS "tools" RESOURCE_LOCK "nexigen-heavy-com" RUN_SERIAL TRUE)

    add_test(NAME nexigen_sm_required_spec
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-sm/required_spec_test.sh
                $<TARGET_FILE:nexigen-sm>)
    set_tests_properties(nexigen_sm_required_spec PROPERTIES
        TIMEOUT 60 LABELS "tools")

    # CAN hardware tools — virtual backend in this change; the vendor hardware
    # adapter is supplied by the interface module.
    add_executable(nexigen-canrx nexigen-canrx/main.cpp)
    target_link_libraries(nexigen-canrx PRIVATE nexigen_hw_can_com nexigen_app Threads::Threads)
    add_test(NAME nexigen_canrx_help COMMAND nexigen-canrx --help)
    set_tests_properties(nexigen_canrx_help PROPERTIES TIMEOUT 60 LABELS "tools;can")

    add_executable(nexigen-cantx nexigen-cantx/main.cpp)
    target_link_libraries(nexigen-cantx PRIVATE nexigen_hw_can_com nexigen_app Threads::Threads)
    add_test(NAME nexigen_cantx_help COMMAND nexigen-cantx --help)
    set_tests_properties(nexigen_cantx_help PROPERTIES TIMEOUT 60 LABELS "tools;can")

    add_executable(nexigen-canmon nexigen-canmon/main.cpp)
    target_link_libraries(nexigen-canmon PRIVATE nexigen_hw_can_com nexigen_app Threads::Threads)
    add_test(NAME nexigen_canmon_help COMMAND nexigen-canmon --help)
    set_tests_properties(nexigen_canmon_help PROPERTIES TIMEOUT 60 LABELS "tools;can")

    add_executable(nexigen-canvbus nexigen-canvbus/main.cpp)
    target_link_libraries(nexigen-canvbus PRIVATE nexigen_hw_can_com nexigen_app Threads::Threads)
    add_test(NAME nexigen_canvbus_help COMMAND nexigen-canvbus --help)
    set_tests_properties(nexigen_canvbus_help PROPERTIES TIMEOUT 60 LABELS "tools;can")

    # Hardware adapter skeletons: fill VendorCanDriver, then use the same
    # CanReceiver/CanTransmitter data path as the virtual tools.
    add_executable(nexigen-canrx-hw nexigen-canrx-hw/main.cpp)
    target_link_libraries(nexigen-canrx-hw PRIVATE nexigen_hw_can_com nexigen_app Threads::Threads)
    target_include_directories(nexigen-canrx-hw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME nexigen_canrx_hw_help COMMAND nexigen-canrx-hw --help)
    set_tests_properties(nexigen_canrx_hw_help PROPERTIES TIMEOUT 60 LABELS "tools;can")

    add_executable(nexigen-cantx-hw nexigen-cantx-hw/main.cpp)
    target_link_libraries(nexigen-cantx-hw PRIVATE nexigen_hw_can_com nexigen_app Threads::Threads)
    target_include_directories(nexigen-cantx-hw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME nexigen_cantx_hw_help COMMAND nexigen-cantx-hw --help)
    set_tests_properties(nexigen_cantx_hw_help PROPERTIES TIMEOUT 60 LABELS "tools;can")

    install(TARGETS nexigen-canrx nexigen-cantx nexigen-canmon nexigen-canvbus
            nexigen-canrx-hw nexigen-cantx-hw
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

    # nexigen-recd — telemetry session recorder (change record-telemetry-sessions).
    add_executable(nexigen-recd nexigen-recd/main.cpp)
    target_link_libraries(nexigen-recd PRIVATE
        nexigen_recorder nexigen_com nexigen_chrono Threads::Threads)

    add_test(NAME nexigen_recd_help COMMAND nexigen-recd --help)
    set_tests_properties(nexigen_recd_help PROPERTIES
        WILL_FAIL TRUE TIMEOUT 60 LABELS "tools")

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

    # Test-only publisher for the smoke test (not installed): the daemon
    # requires a published base.
    add_executable(nexigen_traced_timebase_fixture nexigen-traced/timebase_fixture.cpp)
    target_link_libraries(nexigen_traced_timebase_fixture PRIVATE nexigen_chrono Threads::Threads)

    add_test(NAME nexigen_traced_help COMMAND nexigen-traced --help)
    set_tests_properties(nexigen_traced_help PROPERTIES
        TIMEOUT 60 LABELS "tools")

    add_test(NAME nexigen_traced_smoke
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-traced/smoke_test.sh
                $<TARGET_FILE:nexigen-traced>
                $<TARGET_FILE:nexigen_traced_timebase_fixture>)
    set_tests_properties(nexigen_traced_smoke PROPERTIES
        TIMEOUT 60 LABELS "tools")

    install(TARGETS nexigen-phmd nexigen-sm nexigen-em nexigen-traced
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

install(TARGETS nexigen-exec nexigen-hub
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# nexigen-export — on-demand raw-session converter (change add-recorder-raw-format).
# The storage library never links MCAP or media; optional MCAP output lives in
# this tool behind NEXIGEN_ENABLE_MCAP. MP4/MKV remux is a follow-up.
add_executable(nexigen-export nexigen-export/main.cpp)
target_link_libraries(nexigen-export PRIVATE
    nexigen_recorder nexigen_logging nexigen_metrics nexigen_tracing Threads::Threads)
target_include_directories(nexigen-export PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME nexigen_export_help COMMAND nexigen-export --help)
set_tests_properties(nexigen_export_help PROPERTIES TIMEOUT 60 LABELS "tools")

# Optional MCAP output for nexigen-export. MCAP stays out of nexigen_recorder;
# its implementation TU is compiled with exceptions and linked only here.
if(NEXIGEN_ENABLE_MCAP)
    include(FetchContent)
    cmake_policy(SET CMP0169 OLD)
    FetchContent_Declare(nx_mcap_export
        URL https://codeload.github.com/foxglove/mcap/tar.gz/42aea98d01dbbfe14c53f787378ee3d70ddb2e61
        URL_HASH SHA256=b4dbe68b88244365cc4627c5a88f6ed517e1ad4e5d5ed50236f80d3ade9d501e
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(nx_mcap_export)
    if(NOT nx_mcap_export_POPULATED)
        FetchContent_Populate(nx_mcap_export)
    endif()
    add_library(nx_mcap_export INTERFACE)
    target_include_directories(nx_mcap_export SYSTEM INTERFACE
        ${nx_mcap_export_SOURCE_DIR}/cpp/mcap/include)
    target_compile_definitions(nx_mcap_export INTERFACE
        MCAP_COMPRESSION_NO_ZSTD MCAP_COMPRESSION_NO_LZ4)
    add_library(nx_mcap_export_impl OBJECT
        nexigen-export/mcap_output.cpp
        nexigen-export/mcap_impl.cpp)
    target_link_libraries(nx_mcap_export_impl PRIVATE
        nx_mcap_export nlohmann_json::nlohmann_json Threads::Threads)
    target_include_directories(nx_mcap_export_impl PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/nexigen-export
        ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(nx_mcap_export_impl PRIVATE
        JSON_NOEXCEPTION=1 _GNU_SOURCE _POSIX_C_SOURCE=200809L)
    target_compile_options(nx_mcap_export_impl PRIVATE -fexceptions)
    target_link_libraries(nexigen-export PRIVATE nx_mcap_export nx_mcap_export_impl)
    target_compile_definitions(nexigen-export PRIVATE NEXIGEN_HAS_MCAP=1)
endif()

install(TARGETS nexigen-export RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Converter CLI regression suite: the test writes a real raw session with the
# recorder library, then drives the installed `nexigen-export` binary through
# every subcommand and checks exit codes, inventories, extraction payloads,
# format rejections and verify diagnostics. MCAP is exercised when the option
# is on, and asserted to fail with the documented message when it is off.
add_executable(nexigen_export_cli_tests nexigen-export/export_cli_test.cpp)
target_link_libraries(nexigen_export_cli_tests PRIVATE
    nexigen_recorder nexigen_tracing Threads::Threads)
add_dependencies(nexigen_export_cli_tests nexigen-export)

if(NEXIGEN_ENABLE_MCAP)
    set(NEXIGEN_EXPORT_CLI_MCAP mcap-on)
else()
    set(NEXIGEN_EXPORT_CLI_MCAP mcap-off)
endif()
add_test(NAME nexigen_export_cli
    COMMAND nexigen_export_cli_tests $<TARGET_FILE:nexigen-export>
            ${NEXIGEN_EXPORT_CLI_MCAP})
set_tests_properties(nexigen_export_cli PROPERTIES TIMEOUT 300 LABELS "tools;recorder")
