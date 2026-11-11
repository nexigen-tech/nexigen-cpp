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

    install(TARGETS nexigen-phmd nexigen-sm
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

install(TARGETS nexigen-exec nexigen-hub
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
