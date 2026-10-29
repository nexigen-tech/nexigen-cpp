# Юнит-тесты nexigen::core: один TU с zero-dependency харнесом (см. design.md D4).
add_executable(nexigen_core_tests core/core_test.cpp)
target_link_libraries(nexigen_core_tests PRIVATE nexigen_core)

add_test(NAME nexigen_core_tests COMMAND nexigen_core_tests)
# Поднять таймаут: 1M-элементный стресс-тест медленно работает под TSan.
set_tests_properties(nexigen_core_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты nexigen::chrono: один TU (тот же харнес D4): timestamp/stamp/
# utc_offset/time_format; calibrate() спит ~2 мс — потоки линкуем.
add_executable(nexigen_chrono_tests chrono/chrono_test.cpp)
target_link_libraries(nexigen_chrono_tests PRIVATE nexigen_chrono Threads::Threads)

add_test(NAME nexigen_chrono_tests COMMAND nexigen_chrono_tests)
set_tests_properties(nexigen_chrono_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты nexigen::logging: один TU (design.md D18/D4), потоки и аффинность
# требуют полного времени; тот же увеличенный таймаут под TSan.
add_executable(nexigen_logging_tests logging/logging_test.cpp)
target_link_libraries(nexigen_logging_tests PRIVATE nexigen_logging Threads::Threads)
# Ворота латентности ≤20 ns включаются только в Release/RelWithDebInfo (design D18):
# числа под Debug/санитайзерами нерепрезентативны, тест тогда проверяет только
# корректность (bounded-time, non-blocking), но не наносекунды.
target_compile_definitions(nexigen_logging_tests PRIVATE
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NEXIGEN_LOGGING_STRICT_LATENCY>)

add_test(NAME nexigen_logging_tests COMMAND nexigen_logging_tests)
set_tests_properties(nexigen_logging_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты nexigen::metrics: один TU (design.md D16), потоки и аффинность
# требуют полного времени; тот же увеличенный таймаут под TSan.
add_executable(nexigen_metrics_tests metrics/metrics_test.cpp)
target_link_libraries(nexigen_metrics_tests PRIVATE nexigen_metrics Threads::Threads)
# Ворота латентности включаются только в Release/RelWithDebInfo (design D16):
# числа под Debug/санитайзерами нерепрезентативны, тест тогда проверяет только
# корректность (bounded-time, non-blocking), но не наносекунды.
target_compile_definitions(nexigen_metrics_tests PRIVATE
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NEXIGEN_METRICS_STRICT_LATENCY>)

add_test(NAME nexigen_metrics_tests COMMAND nexigen_metrics_tests)
set_tests_properties(nexigen_metrics_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты nexigen::tracing: один TU (design D14), потоки и аффинность
# требуют полного времени; тот же увеличенный таймаут под TSan.
add_executable(nexigen_tracing_tests tracing/tracing_test.cpp)
target_link_libraries(nexigen_tracing_tests PRIVATE nexigen_tracing Threads::Threads)
# Ворота латентности (≤20 ns lifecycle / ≤2 ns short-circuit) включаются
# только в Release/RelWithDebInfo (design D14): числа под Debug/санитайзерами
# нерепрезентативны, тест тогда проверяет только корректность.
target_compile_definitions(nexigen_tracing_tests PRIVATE
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NEXIGEN_TRACING_STRICT_LATENCY>)

add_test(NAME nexigen_tracing_tests COMMAND nexigen_tracing_tests)
set_tests_properties(nexigen_tracing_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты nexigen::diagnostics: один TU (тот же харнес D4): enum-контракт
# сериализации, Stopwatch, ScopedStopwatch.
add_executable(nexigen_diagnostics_tests diagnostics/diagnostics_test.cpp)
target_link_libraries(nexigen_diagnostics_tests PRIVATE nexigen_diagnostics)
# Ворота латентности включаются только в Release/RelWithDebInfo (design D10):
# числа под Debug/санитайзерами нерепрезентативны, тест тогда проверяет только
# корректность (bounded-time, non-blocking), но не наносекунды.
target_compile_definitions(nexigen_diagnostics_tests PRIVATE
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NEXIGEN_DIAGNOSTICS_STRICT_LATENCY>)

add_test(NAME nexigen_diagnostics_tests COMMAND nexigen_diagnostics_tests)
set_tests_properties(nexigen_diagnostics_tests PROPERTIES TIMEOUT 600)
