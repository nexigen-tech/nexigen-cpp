# Shared GUI dependencies for the desktop tools (change add-can-monitor-view).
#
# Dear ImGui, GLFW and ImPlot are pinned once here and exposed as the
# `nexigen_imgui`, `nexigen_glfw` and `nexigen_implot` targets. Tools link the
# hoisted targets instead of declaring their own FetchContent blocks, so the
# log viewer and the CAN viewer always agree on one revision.
#
# Include this module only when at least one GUI tool is enabled; headless
# builds must stay free of OpenGL/GLFW/X11 dependencies.

if(NEXIGEN_GUI_TARGETS_READY)
    return()
endif()
set(NEXIGEN_GUI_TARGETS_READY ON)

include(FetchContent)

# Dear ImGui docking branch, pinned commit (2026-11-20). The bundled GL loader
# is used; no glad dependency.
FetchContent_Declare(nx_imgui
    SYSTEM
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG b1c1666c858cf3decb5b8de75255a927a7ec5361)
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

# ImPlot v1.2 WIP, pinned commit (2026-10-25) compatible with the pinned ImGui
# docking revision above.
FetchContent_Declare(nx_implot
    SYSTEM
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG 72de27832d1e6299bcc930ce01073d899d420392
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nx_implot)

add_library(nexigen_implot STATIC
    ${nx_implot_SOURCE_DIR}/implot.cpp
    ${nx_implot_SOURCE_DIR}/implot_items.cpp)
target_include_directories(nexigen_implot SYSTEM PUBLIC
    ${nx_implot_SOURCE_DIR})
target_link_libraries(nexigen_implot PUBLIC nexigen_imgui)

find_package(OpenGL REQUIRED)
find_package(PNG 1.6 REQUIRED)

# Stable wrapper names for consumers; the upstream GLFW target stays an
# implementation detail of the hoist.
add_library(nexigen_glfw INTERFACE)
target_link_libraries(nexigen_glfw INTERFACE glfw)
target_link_libraries(nexigen_imgui PUBLIC nexigen_glfw OpenGL::GL)
