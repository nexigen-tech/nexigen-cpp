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

    install(TARGETS nexigen-phmd
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

install(TARGETS nexigen-exec nexigen-hub
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
