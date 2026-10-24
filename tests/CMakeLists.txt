# Юнит-тесты nexigen::core: один TU с zero-dependency харнесом (см. design.md D4).
add_executable(nexigen_core_tests core/core_test.cpp)
target_link_libraries(nexigen_core_tests PRIVATE nexigen_core)

add_test(NAME nexigen_core_tests COMMAND nexigen_core_tests)
# Поднять таймаут: 1M-элементный стресс-тест медленно работает под TSan.
set_tests_properties(nexigen_core_tests PROPERTIES TIMEOUT 600)
