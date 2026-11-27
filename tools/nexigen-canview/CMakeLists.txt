# nexigen-canview — read-only realtime CAN/CAN-FD monitor.
#
# Scaffolding for change add-can-monitor-view. The model and COM layers build
# as separate static libraries so headless tests never link OpenGL/GLFW; only
# the application target links the shared GUI targets.

set(NEXIGEN_CANVIEW_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# --- model: bounded history, views, filters, time, session -----------------
set(NEXIGEN_CANVIEW_MODEL_SOURCES
    canopen/canopen_decoder.cpp
    canopen/canopen_reindex.cpp
    canopen/canopen_registry.cpp
    canopen/co_decoded.cpp
    canopen/co_runtime.cpp
    canopen/cob_id_router.cpp
    canopen/eds_library.cpp
    canopen/emcy_decoder.cpp
    canopen/nmt_decoder.cpp
    canopen/pdo_decoder.cpp
    canopen/sdo_decoder.cpp
    model/anchor_table.cpp
    model/bit_activity.cpp
    model/bit_timing.cpp
    model/budget.cpp
    model/can_ingress_record.cpp
    model/can_record_ring.cpp
    model/can_record_view.cpp
    model/can_view.cpp
    model/can_view_model.cpp
    model/canview_config.cpp
    model/capture_stats.cpp
    model/filter_criteria.cpp
    model/find_state.cpp
    model/heatmap.cpp
    model/overwrite_key.cpp
    model/overwrite_table.cpp
    model/resource_guard.cpp
    model/rtti_anchor.cpp
    model/sequence_tracker.cpp
    model/slot_bitset.cpp
    model/session_export.cpp
    model/session_import.cpp
    model/session_record.cpp
    model/session_replay.cpp
    model/status_snapshot.cpp
    model/snapshot_source.cpp
    model/time_presenter.cpp
    model/validate_for_view.cpp
    model/version.cpp
    model/view_channel.cpp
    model/visible_index.cpp)

add_library(nexigen_canview_model STATIC ${NEXIGEN_CANVIEW_MODEL_SOURCES})
add_library(nexigen::canview::model ALIAS nexigen_canview_model)
target_include_directories(nexigen_canview_model PUBLIC
    $<BUILD_INTERFACE:${NEXIGEN_CANVIEW_DIR}>
    $<BUILD_INTERFACE:${NEXIGEN_CANVIEW_DIR}/model>
    $<BUILD_INTERFACE:${NEXIGEN_CANVIEW_DIR}/canopen>)
target_compile_definitions(nexigen_canview_model PUBLIC
    $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
# The test target links nexigen::com, whose iceoryx2 dependency enables RTTI;
# keep the model on the same RTTI setting so polymorphic typeinfo matches.
target_compile_options(nexigen_canview_model PRIVATE -frtti)
set_source_files_properties(model/rtti_anchor.cpp model/snapshot_source.cpp PROPERTIES COMPILE_OPTIONS "-frtti")
target_link_libraries(nexigen_canview_model PUBLIC
    nexigen_core nexigen_chrono nexigen_hw_can nexigen_canopen PRIVATE Threads::Threads)

# --- com: capture thread and bounded queues --------------------------------
set(NEXIGEN_CANVIEW_COM_SOURCES
    backend/anchor_estimator.cpp
    backend/capture_thread.cpp)

add_library(nexigen_canview_com STATIC ${NEXIGEN_CANVIEW_COM_SOURCES})
add_library(nexigen::canview::com ALIAS nexigen_canview_com)
target_include_directories(nexigen_canview_com PUBLIC
    $<BUILD_INTERFACE:${NEXIGEN_CANVIEW_DIR}>
    $<BUILD_INTERFACE:${NEXIGEN_CANVIEW_DIR}/backend>)
target_link_libraries(nexigen_canview_com PUBLIC
    nexigen_canview_model nexigen_com nexigen_hw_can_com
    PRIVATE Threads::Threads)

# --- headless model + transport tests (no OpenGL, no window) ---------------
add_executable(nexigen_canview_tests
    tests/main.cpp
    tests/canopen_decode_test.cpp
    tests/heatmap_test.cpp
    tests/canopen_test.cpp
    tests/capture_test.cpp
    tests/cli_test.cpp
    tests/config_test.cpp
    tests/model_test.cpp
    tests/resources_test.cpp
    tests/ring_test.cpp
    tests/session_test.cpp
    tests/time_test.cpp
    tests/views_test.cpp
    cli.cpp)
target_link_libraries(nexigen_canview_tests PRIVATE
    nexigen_canview_com nexigen_canview_model nexigen_com Threads::Threads)
target_include_directories(nexigen_canview_tests PRIVATE
    ${NEXIGEN_CANVIEW_DIR} ${CMAKE_SOURCE_DIR}/tests)
add_test(NAME nexigen_canview_tests COMMAND nexigen_canview_tests)
set_tests_properties(nexigen_canview_tests PROPERTIES TIMEOUT 600 LABELS "tools;canview")

# --- application -----------------------------------------------------------
set(NEXIGEN_CANVIEW_APP_SOURCES
    main.cpp
    cli.cpp
    headless.cpp)

add_executable(nexigen-canview ${NEXIGEN_CANVIEW_APP_SOURCES})
target_link_libraries(nexigen-canview PRIVATE
    nexigen_canview_com nexigen_canview_model nexigen_app Threads::Threads)
target_include_directories(nexigen-canview PRIVATE
    ${NEXIGEN_CANVIEW_DIR}
    ${NEXIGEN_CANVIEW_DIR}/model
    ${NEXIGEN_CANVIEW_DIR}/backend)

if(NEXIGEN_CANVIEW_GUI)
    target_link_libraries(nexigen-canview PRIVATE
        nexigen_imgui nexigen_implot nexigen_glfw PNG::PNG)
    target_compile_definitions(nexigen-canview PRIVATE NEXIGEN_CANVIEW_GUI=1)
    target_include_directories(nexigen-canview PRIVATE
        ${NEXIGEN_CANVIEW_DIR}/ui)
    target_sources(nexigen-canview PRIVATE
        ui/canview_window.cpp
        ui/splash_image.cpp
        ui/theme.cpp)
    set(NEXIGEN_CANVIEW_FONT_HEADER
        "${CMAKE_CURRENT_BINARY_DIR}/generated/jetbrains_mono_asset.hpp")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(READ "${NEXIGEN_CANVIEW_DIR}/assets/JetBrainsMono-Regular.ttf" NX_FONT_HEX HEX)
    string(LENGTH "${NX_FONT_HEX}" NX_FONT_HEX_LENGTH)
    math(EXPR NX_FONT_SIZE "${NX_FONT_HEX_LENGTH} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," NX_FONT_BYTES "${NX_FONT_HEX}")
    file(WRITE "${NEXIGEN_CANVIEW_FONT_HEADER}"
"// Generated from assets/JetBrainsMono-Regular.ttf (OFL).\n"
"#pragma once\n"
"#include <array>\n"
"namespace nexigen::canview::assets {\n"
"inline constexpr std::array<unsigned char, ${NX_FONT_SIZE}> kJetBrainsMono{${NX_FONT_BYTES}};\n"
"}\n")
    set(NEXIGEN_CANVIEW_SPLASH_HEADER
        "${CMAKE_CURRENT_BINARY_DIR}/generated/nexigen_splash_asset.hpp")
    file(READ "${NEXIGEN_CANVIEW_DIR}/assets/nexigen_splash.png" NX_SPLASH_HEX HEX)
    string(LENGTH "${NX_SPLASH_HEX}" NX_SPLASH_HEX_LENGTH)
    math(EXPR NX_SPLASH_SIZE "${NX_SPLASH_HEX_LENGTH} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," NX_SPLASH_BYTES "${NX_SPLASH_HEX}")
    file(WRITE "${NEXIGEN_CANVIEW_SPLASH_HEADER}"
"// Generated from assets/nexigen_splash.png.\n"
"#pragma once\n"
"#include <array>\n"
"namespace nexigen::canview::assets {\n"
"inline constexpr std::array<unsigned char, ${NX_SPLASH_SIZE}> kCanViewSplashPng{${NX_SPLASH_BYTES}};\n"
"}\n")
    target_include_directories(nexigen-canview PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated")

    # Compile-only proof that the pinned ImPlot revision matches the pinned
    # ImGui docking revision (task 1.3).
    add_library(nexigen_canview_implot_smoke OBJECT tests/implot_smoke.cpp)
    target_link_libraries(nexigen_canview_implot_smoke PRIVATE nexigen_implot)

    set(NEXIGEN_CANVIEW_UI_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/ui-test-config.json")
    set(NEXIGEN_CANVIEW_UI_FIXTURE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/offline.jsonl")

    add_test(NAME nexigen_canview_ui_smoke
        COMMAND xvfb-run -a $<TARGET_FILE:nexigen-canview>
                --config ${NEXIGEN_CANVIEW_UI_CONFIG} --ui-smoke)
    set_tests_properties(nexigen_canview_ui_smoke PROPERTIES
        TIMEOUT 120 LABELS "tools;canview;gui"
        PASS_REGULAR_EXPRESSION "UI_SMOKE")

    add_test(NAME nexigen_canview_ui_splash
        COMMAND xvfb-run -a $<TARGET_FILE:nexigen-canview>
                --config ${NEXIGEN_CANVIEW_UI_CONFIG} --ui-splash-test)
    set_tests_properties(nexigen_canview_ui_splash PROPERTIES
        TIMEOUT 120 LABELS "tools;canview;gui"
        PASS_REGULAR_EXPRESSION "UI_SPLASH")

    add_test(NAME nexigen_canview_ui_scale
        COMMAND xvfb-run -a $<TARGET_FILE:nexigen-canview>
                --config ${NEXIGEN_CANVIEW_UI_CONFIG} --ui-smoke --ui-scale 2)
    set_tests_properties(nexigen_canview_ui_scale PROPERTIES
        TIMEOUT 120 LABELS "tools;canview;gui"
        PASS_REGULAR_EXPRESSION "UI_SCALE scale=2.00")

    add_test(NAME nexigen_canview_ui_actions
        COMMAND xvfb-run -a $<TARGET_FILE:nexigen-canview>
                --config ${NEXIGEN_CANVIEW_UI_CONFIG} --ui-actions-test
                --offline ${NEXIGEN_CANVIEW_UI_FIXTURE})
    set_tests_properties(nexigen_canview_ui_actions PROPERTIES
        TIMEOUT 120 LABELS "tools;canview;gui"
        PASS_REGULAR_EXPRESSION "UI_ACTIONS selected=1")

    add_test(NAME nexigen_canview_ui_views
        COMMAND xvfb-run -a $<TARGET_FILE:nexigen-canview>
                --config ${NEXIGEN_CANVIEW_UI_CONFIG} --ui-view-test
                --offline ${NEXIGEN_CANVIEW_UI_FIXTURE})
    set_tests_properties(nexigen_canview_ui_views PROPERTIES
        TIMEOUT 120 LABELS "tools;canview;gui"
        PASS_REGULAR_EXPRESSION "UI_VIEWS live=1 paused=1")

    add_test(NAME nexigen_canview_ui_focus
        COMMAND xvfb-run -a $<TARGET_FILE:nexigen-canview>
                --config ${NEXIGEN_CANVIEW_UI_CONFIG} --ui-focus-test
                --offline ${NEXIGEN_CANVIEW_UI_FIXTURE})
    set_tests_properties(nexigen_canview_ui_focus PROPERTIES
        TIMEOUT 120 LABELS "tools;canview;gui"
        PASS_REGULAR_EXPRESSION "UI_FOCUS .*changed=1")

    add_test(NAME nexigen_canview_graceful_shutdown
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/graceful_shutdown_test.sh
                $<TARGET_FILE:nexigen-canview> ${NEXIGEN_CANVIEW_UI_CONFIG}
                ${NEXIGEN_CANVIEW_UI_FIXTURE})
    set_tests_properties(nexigen_canview_graceful_shutdown PROPERTIES
        TIMEOUT 120 LABELS "tools;canview;gui" RESOURCE_LOCK "nexigen-heavy-com" RUN_SERIAL TRUE)

    add_test(NAME nexigen_canview_ui_input
        COMMAND xvfb-run -a $<TARGET_FILE:nexigen-canview>
                --config ${NEXIGEN_CANVIEW_UI_CONFIG} --ui-input-test
                --offline ${NEXIGEN_CANVIEW_UI_FIXTURE})
    set_tests_properties(nexigen_canview_ui_input PROPERTIES
        TIMEOUT 120 LABELS "tools;canview;gui"
        PASS_REGULAR_EXPRESSION "UI_INPUT paused=1")
endif()

add_test(NAME nexigen_canview_help COMMAND nexigen-canview --help)
set_tests_properties(nexigen_canview_help PROPERTIES TIMEOUT 60 LABELS "tools;canview")

add_test(NAME nexigen_canview_invalid_args COMMAND nexigen-canview --no-such-option)
set_tests_properties(nexigen_canview_invalid_args PROPERTIES
    WILL_FAIL TRUE TIMEOUT 60 LABELS "tools;canview")

set(NEXIGEN_CANVIEW_TEST_CONFIG
    "${CMAKE_CURRENT_BINARY_DIR}/headless-config/canview.json")

add_test(NAME nexigen_canview_headless_mock
    COMMAND nexigen-canview --config ${NEXIGEN_CANVIEW_TEST_CONFIG}
            --headless --mock 10 --max-records 3)
set_tests_properties(nexigen_canview_headless_mock PROPERTIES
    TIMEOUT 120 LABELS "tools;canview"
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/headless-config")

add_test(NAME nexigen_canview_headless_offline
    COMMAND nexigen-canview --config ${NEXIGEN_CANVIEW_TEST_CONFIG}
            --headless --offline ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/offline.jsonl
            --max-records 5)
set_tests_properties(nexigen_canview_headless_offline PROPERTIES
    TIMEOUT 120 LABELS "tools;canview"
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/headless-config")

add_test(NAME nexigen_canview_headless_empty
    COMMAND nexigen-canview --config ${NEXIGEN_CANVIEW_TEST_CONFIG} --headless)
set_tests_properties(nexigen_canview_headless_empty PROPERTIES
    WILL_FAIL TRUE TIMEOUT 60 LABELS "tools;canview"
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/headless-config")

add_test(NAME nexigen_canview_headless_soak
    COMMAND nexigen-canview --config ${NEXIGEN_CANVIEW_TEST_CONFIG}
            --headless --soak 1 --rate 1000 --max-records 2)
set_tests_properties(nexigen_canview_headless_soak PROPERTIES
    TIMEOUT 60 LABELS "tools;canview"
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/headless-config")

add_test(NAME nexigen_canview_acceptance
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance_test.sh
            $<TARGET_FILE:nexigen-canview> ${CMAKE_CURRENT_BINARY_DIR}/headless-config
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/offline.jsonl
            ${CMAKE_CURRENT_BINARY_DIR}/acceptance)
set_tests_properties(nexigen_canview_acceptance PROPERTIES
    TIMEOUT 180 LABELS "tools;canview")

add_test(NAME nexigen_canview_headless_steady
    COMMAND nexigen-canview --config ${NEXIGEN_CANVIEW_TEST_CONFIG}
            --headless --soak 1 --rate 5000 --max-records 1)
set_tests_properties(nexigen_canview_headless_steady PROPERTIES
    TIMEOUT 60 LABELS "tools;canview"
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/headless-config")

add_test(NAME nexigen_canview_headless_burst
    COMMAND nexigen-canview --config ${NEXIGEN_CANVIEW_TEST_CONFIG}
            --headless --soak 1 --rate 50000 --max-records 1)
set_tests_properties(nexigen_canview_headless_burst PROPERTIES
    TIMEOUT 60 LABELS "tools;canview"
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/headless-config")

add_test(NAME nexigen_canview_exit_codes
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/exit_code_test.sh
            $<TARGET_FILE:nexigen-canview> ${NEXIGEN_CANVIEW_TEST_CONFIG}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/offline.jsonl)
set_tests_properties(nexigen_canview_exit_codes PROPERTIES
    TIMEOUT 120 LABELS "tools;canview")

install(TARGETS nexigen-canview RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(NEXIGEN_CANVIEW_GUI)
    install(FILES "${NEXIGEN_CANVIEW_DIR}/assets/OFL.txt"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/nexigen/canview")
endif()
