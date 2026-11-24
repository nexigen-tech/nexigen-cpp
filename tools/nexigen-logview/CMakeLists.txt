# nexigen-logview — realtime LogFrame viewer (change add-realtime-log-viewer).
#
# The model library and its headless tests never link OpenGL/GLFW, so CI can
# exercise ring/filter/time/color/import/export/resource logic without a
# display. The GUI target is the only consumer of the pinned ImGui/GLFW block.

set(NEXIGEN_LOGVIEW_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
option(NEXIGEN_LOGVIEW_GUI
    "Build the nexigen-logview GLFW/ImGui window (needs X11/OpenGL development packages)"
    ON)

set(NEXIGEN_LOGVIEW_MODEL_SOURCES
    model/app_registry.cpp
    model/color_manager.cpp
    model/export_record.cpp
    model/export_writer.cpp
    model/filter.cpp
    model/find_state.cpp
    model/history_source.cpp
    model/importer.cpp
    model/ingress_record.cpp
    model/inspector_text.cpp
    model/json_escape.cpp
    model/level_style.cpp
    model/log_view.cpp
    model/log_view_model.cpp
    model/logview_config.cpp
    model/minimap.cpp
    model/record_ring.cpp
    model/resource_guard.cpp
    model/rgba.cpp
    model/time_context.cpp
    model/time_formatter.cpp
    model/ui_actions.cpp)

add_library(nexigen_logview_model STATIC ${NEXIGEN_LOGVIEW_MODEL_SOURCES})
add_library(nexigen::logview::model ALIAS nexigen_logview_model)
target_include_directories(nexigen_logview_model PUBLIC
    $<BUILD_INTERFACE:${NEXIGEN_LOGVIEW_DIR}>
    $<BUILD_INTERFACE:${NEXIGEN_LOGVIEW_DIR}/model>
    $<BUILD_INTERFACE:${NEXIGEN_LOGVIEW_DIR}/backend>)
target_compile_definitions(nexigen_logview_model PUBLIC
    $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
target_link_libraries(nexigen_logview_model PUBLIC
    nexigen_core nexigen_chrono nexigen_logging PRIVATE Threads::Threads)

add_library(nexigen_logview_com STATIC backend/capture_thread.cpp)
add_library(nexigen::logview::com ALIAS nexigen_logview_com)
target_link_libraries(nexigen_logview_com PUBLIC
    nexigen_logview_model nexigen_com nexigen_logging PRIVATE Threads::Threads)

# Headless model and transport tests: no OpenGL, no window.
add_executable(nexigen_logview_tests
    tests/logview_model_test.cpp
    cli.cpp)
target_link_libraries(nexigen_logview_tests PRIVATE
    nexigen_logview_com nexigen_com Threads::Threads)
target_include_directories(nexigen_logview_tests PRIVATE
    ${NEXIGEN_LOGVIEW_DIR} ${CMAKE_SOURCE_DIR}/tests)
add_test(NAME nexigen_logview_tests COMMAND nexigen_logview_tests)
set_tests_properties(nexigen_logview_tests PROPERTIES TIMEOUT 600 LABELS "tools;logview")

# Capture/transport smoke: a real iceoryx2 publisher feeds a CaptureThread and
# the model; skipped cleanly when the transport cannot start in this sandbox.
add_executable(nexigen_logview_transport_tests tests/logview_transport_test.cpp)
target_link_libraries(nexigen_logview_transport_tests PRIVATE
    nexigen_logview_com nexigen_com Threads::Threads)
add_test(NAME nexigen_logview_transport_tests COMMAND nexigen_logview_transport_tests)
set_tests_properties(nexigen_logview_transport_tests PROPERTIES TIMEOUT 120 LABELS "tools;logview")

# Headless CLI (also the binary used by the GUI target below).
set(NEXIGEN_LOGVIEW_APP_SOURCES
    main.cpp
    cli.cpp
    headless.cpp)
if(NEXIGEN_LOGVIEW_GUI)
    list(APPEND NEXIGEN_LOGVIEW_APP_SOURCES
        ui/logview_window.cpp
        ui/splash_image.cpp
        ui/theme.cpp)
endif()

add_executable(nexigen-logview ${NEXIGEN_LOGVIEW_APP_SOURCES})
target_link_libraries(nexigen-logview PRIVATE
    nexigen_logview_com nexigen_logview_model nexigen_app Threads::Threads)
target_include_directories(nexigen-logview PRIVATE
    ${NEXIGEN_LOGVIEW_DIR}
    ${NEXIGEN_LOGVIEW_DIR}/model
    ${NEXIGEN_LOGVIEW_DIR}/backend)

add_test(NAME nexigen_logview_help COMMAND nexigen-logview --help)
set_tests_properties(nexigen_logview_help PROPERTIES TIMEOUT 60 LABELS "tools;logview")

add_test(NAME nexigen_logview_invalid_args COMMAND nexigen-logview --no-such-option)
set_tests_properties(nexigen_logview_invalid_args PROPERTIES
    WILL_FAIL TRUE TIMEOUT 60 LABELS "tools;logview")

add_test(NAME nexigen_logview_ui_scale_cli
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_scale_cli_test.sh
            $<TARGET_FILE:nexigen-logview>)
set_tests_properties(nexigen_logview_ui_scale_cli PROPERTIES
    TIMEOUT 60 LABELS "tools;logview")

add_test(NAME nexigen_logview_headless_mock
    COMMAND nexigen-logview --headless --mock 50000 --max-records 50000)
set_tests_properties(nexigen_logview_headless_mock PROPERTIES
    TIMEOUT 120 LABELS "tools;logview")

set(NEXIGEN_LOGVIEW_FIXTURE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/offline.jsonl")
add_test(NAME nexigen_logview_headless_offline
    COMMAND nexigen-logview --headless --offline ${NEXIGEN_LOGVIEW_FIXTURE} --max-records 3)
set_tests_properties(nexigen_logview_headless_offline PROPERTIES
    TIMEOUT 60 LABELS "tools;logview")

if(NEXIGEN_LOGVIEW_GUI)
    include(FetchContent)

    # Dear ImGui docking branch, pinned commit (2026-11-23). The bundled GL
    # loader is used; no glad dependency.
    FetchContent_Declare(nx_imgui
        SYSTEM
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG b1c1666c858cf3decb5b8de75255a927a7ec5361
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(nx_imgui)

    add_library(nexigen_imgui STATIC
        ${nx_imgui_SOURCE_DIR}/imgui.cpp
        ${nx_imgui_SOURCE_DIR}/imgui_draw.cpp
        ${nx_imgui_SOURCE_DIR}/imgui_tables.cpp
        ${nx_imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${nx_imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${nx_imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(nexigen_imgui SYSTEM PUBLIC
        ${nx_imgui_SOURCE_DIR}
        ${nx_imgui_SOURCE_DIR}/backends)

    # GLFW 3.4, pinned archive. Only the windowing/X11 backend is needed; the
    # examples, tests, docs, and install rules stay off.
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    FetchContent_Declare(nx_glfw
        SYSTEM
        URL https://github.com/glfw/glfw/archive/refs/tags/3.4.zip
        URL_HASH SHA256=a133ddc3d3c66143eba9035621db8e0bcf34dba1ee9514a9e23e96afd39fd57a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nx_glfw)

    find_package(OpenGL REQUIRED)
    find_package(PNG 1.6 REQUIRED)
    target_link_libraries(nexigen_imgui PUBLIC glfw OpenGL::GL)
    target_link_libraries(nexigen-logview PRIVATE nexigen_imgui PNG::PNG)
    target_compile_definitions(nexigen-logview PRIVATE NEXIGEN_LOGVIEW_GUI=1)

    # Embed the OFL-licensed JetBrains Mono TTF at configure time so the binary
    # never depends on a system font. The generated TU lives in the build tree.
    set(NEXIGEN_LOGVIEW_FONT_HEADER
        "${CMAKE_CURRENT_BINARY_DIR}/generated/jetbrains_mono_asset.hpp")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(READ "${NEXIGEN_LOGVIEW_DIR}/assets/JetBrainsMono-Regular.ttf" NX_FONT_HEX HEX)
    string(LENGTH "${NX_FONT_HEX}" NX_FONT_HEX_LENGTH)
    math(EXPR NX_FONT_SIZE "${NX_FONT_HEX_LENGTH} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," NX_FONT_BYTES "${NX_FONT_HEX}")
    file(WRITE "${NEXIGEN_LOGVIEW_FONT_HEADER}"
"// Generated from assets/JetBrainsMono-Regular.ttf (OFL, see assets/OFL.txt).\n"
"#pragma once\n"
"#include <array>\n"
"#include <cstddef>\n"
"namespace nexigen::logview::assets {\n"
"inline constexpr std::array<unsigned char, ${NX_FONT_SIZE}> kJetBrainsMono{${NX_FONT_BYTES}};\n"
"}\n")
    target_include_directories(nexigen-logview PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated")

    # Embed the startup splash PNG so the executable is self-contained.
    set(NEXIGEN_LOGVIEW_SPLASH_HEADER
        "${CMAKE_CURRENT_BINARY_DIR}/generated/nexigen_splash_asset.hpp")
    file(READ "${NEXIGEN_LOGVIEW_DIR}/assets/nexigen_splash.png" NX_SPLASH_HEX HEX)
    string(LENGTH "${NX_SPLASH_HEX}" NX_SPLASH_HEX_LENGTH)
    math(EXPR NX_SPLASH_SIZE "${NX_SPLASH_HEX_LENGTH} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," NX_SPLASH_BYTES "${NX_SPLASH_HEX}")
    file(WRITE "${NEXIGEN_LOGVIEW_SPLASH_HEADER}"
"// Generated from assets/nexigen_splash.png.\n"
"#pragma once\n"
"#include <array>\n"
"#include <cstddef>\n"
"namespace nexigen::logview::assets {\n"
"inline constexpr std::array<unsigned char, ${NX_SPLASH_SIZE}> kNexigenSplashPng{${NX_SPLASH_BYTES}};\n"
"}\n")

    add_test(NAME nexigen_logview_ui_smoke
        COMMAND xvfb-run -a $<TARGET_FILE:nexigen-logview> --ui-smoke)
    set_tests_properties(nexigen_logview_ui_smoke PROPERTIES
        TIMEOUT 120 LABELS "tools;logview;gui"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/ui-test-config")

    add_test(NAME nexigen_logview_ui_offline_smoke
        COMMAND xvfb-run -a $<TARGET_FILE:nexigen-logview>
                --offline ${NEXIGEN_LOGVIEW_FIXTURE} --ui-smoke)
    set_tests_properties(nexigen_logview_ui_offline_smoke PROPERTIES
        TIMEOUT 120 LABELS "tools;logview;gui"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/ui-test-config")

    add_test(NAME nexigen_logview_ui_actions
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_actions_test.sh
                $<TARGET_FILE:nexigen-logview>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui-actions.jsonl)
    set_tests_properties(nexigen_logview_ui_actions PROPERTIES
        TIMEOUT 180 LABELS "tools;logview;gui")

    add_test(NAME nexigen_logview_ui_scale
        COMMAND xvfb-run -a $<TARGET_FILE:nexigen-logview> --ui-smoke --ui-scale 2)
    set_tests_properties(nexigen_logview_ui_scale PROPERTIES
        TIMEOUT 120 LABELS "tools;logview;gui"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/ui-test-config")

    add_test(NAME nexigen_logview_ui_input
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_input_test.sh
                $<TARGET_FILE:nexigen-logview>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui-actions.jsonl)
    set_tests_properties(nexigen_logview_ui_input PROPERTIES
        TIMEOUT 180 LABELS "tools;logview;gui")

    add_test(NAME nexigen_logview_ui_views
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_view_test.sh
                $<TARGET_FILE:nexigen-logview>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui-actions.jsonl)
    set_tests_properties(nexigen_logview_ui_views PROPERTIES
        TIMEOUT 180 LABELS "tools;logview;gui")

    add_test(NAME nexigen_logview_ui_focus
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_focus_test.sh
                $<TARGET_FILE:nexigen-logview>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui-actions.jsonl)
    set_tests_properties(nexigen_logview_ui_focus PROPERTIES
        TIMEOUT 180 LABELS "tools;logview;gui")

    add_test(NAME nexigen_logview_ui_splash
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_splash_test.sh
                $<TARGET_FILE:nexigen-logview>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui-actions.jsonl)
    set_tests_properties(nexigen_logview_ui_splash PROPERTIES
        TIMEOUT 180 LABELS "tools;logview;gui")
endif()

install(TARGETS nexigen-logview RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${NEXIGEN_LOGVIEW_DIR}/assets/OFL.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/nexigen/logview")
