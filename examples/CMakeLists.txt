# Runnable, self-contained examples for the public nexigen API.
#
# nexigen_add_example(name source target): one executable per TU, linked
# against the module target and Threads (jthread producers are part of the
# demonstrated API), and registered as a CTest smoke test. The 600 s timeout
# matches the unit-test convention so TSan/ASan runs do not flake.
function(nexigen_add_example name source target)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${target} Threads::Threads)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 600 LABELS "examples")
endfunction()

add_subdirectory(core)
add_subdirectory(logging)
add_subdirectory(metrics)
