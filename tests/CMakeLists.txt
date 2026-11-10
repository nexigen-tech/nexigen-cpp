# Юнит-тесты nexigen::core: один TU с zero-dependency харнесом (см. design.md D4).
add_executable(nexigen_core_tests core/core_test.cpp)
target_link_libraries(nexigen_core_tests PRIVATE nexigen_core)

add_test(NAME nexigen_core_tests COMMAND nexigen_core_tests)
# Поднять таймаут: 1M-элементный стресс-тест медленно работает под TSan.
set_tests_properties(nexigen_core_tests PROPERTIES TIMEOUT 600)

# Negative-compile probe (dedup-sink-plumbing, spec core/sink-plumbing):
# probe TU должен НЕ собраться — static_assert'ы пака обязаны сработать.
# SCR-файл намеренно вне таргетов (см. tests/core/neg/).
add_test(NAME nexigen_sink_pack_negative_compile
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/core/negative_compile.sh
                 ${CMAKE_CXX_COMPILER}
                 ${CMAKE_CURRENT_SOURCE_DIR}/core/neg/sink_pack_negative.cpp
                 ${PROJECT_SOURCE_DIR}/include)
set_tests_properties(nexigen_sink_pack_negative_compile PROPERTIES TIMEOUT 120)

# Юнит-тесты nexigen::chrono: один TU (тот же харнес D4): timestamp/stamp/
# utc_offset/time_format; calibrate() спит ~2 мс — потоки линкуем.
add_executable(nexigen_chrono_tests chrono/chrono_test.cpp)
target_link_libraries(nexigen_chrono_tests PRIVATE nexigen_chrono Threads::Threads)

add_test(NAME nexigen_chrono_tests COMMAND nexigen_chrono_tests)
set_tests_properties(nexigen_chrono_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты nexigen::logging: один TU на сущность (design.md D18/D4), потоки и
# аффинность требуют полного времени; тот же увеличенный таймаут под TSan.
add_executable(nexigen_logging_tests
    logging/main.cpp
    logging/logging_test_support.cpp
    logging/log_level_test.cpp
    logging/log_record_test.cpp
    logging/producer_test.cpp
    logging/latency_test.cpp
    logging/null_logger_test.cpp
    logging/console_logger_test.cpp
    logging/json_format_test.cpp
    logging/affinity_test.cpp
    logging/rotation_test.cpp
    logging/file_logger_test.cpp
    logging/composite_logger_test.cpp
    logging/enum_vocabulary_test.cpp
    logging/logger_options_test.cpp
    logging/spam_protection_test.cpp
    logging/async_worker_test.cpp
    logging/stress_test.cpp
    logging/settings_test.cpp
    logging/worker_test.cpp)
target_link_libraries(nexigen_logging_tests PRIVATE nexigen_logging Threads::Threads)
# Ворота латентности ≤20 ns включаются только в Release/RelWithDebInfo (design D18):
# числа под Debug/санитайзерами нерепрезентативны, тест тогда проверяет только
# корректность (bounded-time, non-blocking), но не наносекунды.
if(NEXIGEN_STRICT_LATENCY_TESTS)
    target_compile_definitions(nexigen_logging_tests PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NEXIGEN_LOGGING_STRICT_LATENCY>)
endif()

add_test(NAME nexigen_logging_tests COMMAND nexigen_logging_tests)
set_tests_properties(nexigen_logging_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты nexigen::metrics: один TU на сущность (design.md D16), потоки и
# аффинность требуют полного времени; тот же увеличенный таймаут под TSan.
add_executable(nexigen_metrics_tests
    metrics/main.cpp
    metrics/metrics_test_support.cpp
    metrics/policy_enums_test.cpp
    metrics/metric_concept_test.cpp
    metrics/meter_test.cpp
    metrics/counter_test.cpp
    metrics/gauge_test.cpp
    metrics/state_gauge_test.cpp
    metrics/descriptive_statistics_test.cpp
    metrics/explicit_histogram_test.cpp
    metrics/exponential_histogram_test.cpp
    metrics/composite_metric_sink_test.cpp
    metrics/metric_provider_test.cpp
    metrics/serialization_test.cpp
    metrics/sinks_test.cpp
    metrics/latency_test.cpp)
target_link_libraries(nexigen_metrics_tests PRIVATE nexigen_metrics Threads::Threads)
# Ворота латентности включаются только в Release/RelWithDebInfo (design D16):
# числа под Debug/санитайзерами нерепрезентативны, тест тогда проверяет только
# корректность (bounded-time, non-blocking), но не наносекунды.
if(NEXIGEN_STRICT_LATENCY_TESTS)
    target_compile_definitions(nexigen_metrics_tests PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NEXIGEN_METRICS_STRICT_LATENCY>)
endif()

add_test(NAME nexigen_metrics_tests COMMAND nexigen_metrics_tests)
set_tests_properties(nexigen_metrics_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты nexigen::tracing: один TU на сущность (design D14), потоки и
# аффинность требуют полного времени; тот же увеличенный таймаут под TSan.
add_executable(nexigen_tracing_tests
    tracing/main.cpp
    tracing/tracing_test_support.cpp
    tracing/span_enums_test.cpp
    tracing/span_context_test.cpp
    tracing/attributes_test.cpp
    tracing/span_record_test.cpp
    tracing/tracer_concept_test.cpp
    tracing/sinks_test.cpp
    tracing/scoped_span_test.cpp
    tracing/latency_test.cpp
    tracing/tracing_settings_test.cpp
    tracing/tracer_provider_test.cpp
    tracing/record_format_test.cpp)
target_link_libraries(nexigen_tracing_tests PRIVATE nexigen_tracing Threads::Threads)
# Ворота латентности (≤20 ns lifecycle / ≤2 ns short-circuit) включаются
# только в Release/RelWithDebInfo (design D14): числа под Debug/санитайзерами
# нерепрезентативны, тест тогда проверяет только корректность.
if(NEXIGEN_STRICT_LATENCY_TESTS)
    target_compile_definitions(nexigen_tracing_tests PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NEXIGEN_TRACING_STRICT_LATENCY>)
endif()

add_test(NAME nexigen_tracing_tests COMMAND nexigen_tracing_tests)
set_tests_properties(nexigen_tracing_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты nexigen::diagnostics: один TU (тот же харнес D4): enum-контракт
# сериализации, Stopwatch, ScopedStopwatch.
add_executable(nexigen_diagnostics_tests diagnostics/diagnostics_test.cpp)
target_link_libraries(nexigen_diagnostics_tests PRIVATE nexigen_diagnostics)
# Ворота латентности включаются только в Release/RelWithDebInfo (design D10):
# числа под Debug/санитайзерами нерепрезентативны, тест тогда проверяет только
# корректность (bounded-time, non-blocking), но не наносекунды.
if(NEXIGEN_STRICT_LATENCY_TESTS)
    target_compile_definitions(nexigen_diagnostics_tests PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NEXIGEN_DIAGNOSTICS_STRICT_LATENCY>)
endif()

add_test(NAME nexigen_diagnostics_tests COMMAND nexigen_diagnostics_tests)
set_tests_properties(nexigen_diagnostics_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты nexigen::app: settings loaders, lifecycle, signalfd watcher.
# Потоки и реальные сигналы требуют полного времени. Один TU на сущность:
# правка настроек не пересобирает инфраструктуру, а падение сразу указывает
# на файл-владелец (см. app_test_support.hpp).
add_executable(nexigen_app_tests
    app/main.cpp
    app/stop_reason_test.cpp
    app/command_line_test.cpp
    app/app_settings_test.cpp
    app/telemetry_layout_test.cpp
    app/infrastructure_test.cpp)
target_link_libraries(nexigen_app_tests PRIVATE nexigen_app Threads::Threads)

add_test(NAME nexigen_app_tests COMMAND nexigen_app_tests)
set_tests_properties(nexigen_app_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты nexigen::exec: создание каталогов, metadata, генерация настроек.
add_executable(nexigen_exec_tests exec/execution_manager_test.cpp)
target_link_libraries(nexigen_exec_tests PRIVATE nexigen_exec Threads::Threads)

add_test(NAME nexigen_exec_tests COMMAND nexigen_exec_tests)
set_tests_properties(nexigen_exec_tests PROPERTIES TIMEOUT 600)

# Юнит-тесты suite-манифеста, политики рестартов и артефактов.
add_executable(nexigen_app_suite_tests exec/app_suite_test.cpp)
target_link_libraries(nexigen_app_suite_tests PRIVATE nexigen_exec Threads::Threads)

add_test(NAME nexigen_app_suite_tests COMMAND nexigen_app_suite_tests)
set_tests_properties(nexigen_app_suite_tests PROPERTIES TIMEOUT 900)

# Юнит-тесты ChildProcess — жизненный цикл одного дочернего приложения.
add_executable(nexigen_child_process_tests exec/child_process_test.cpp)
target_link_libraries(nexigen_child_process_tests PRIVATE nexigen_exec Threads::Threads)

add_test(NAME nexigen_child_process_tests COMMAND nexigen_child_process_tests)
set_tests_properties(nexigen_child_process_tests PROPERTIES TIMEOUT 300)

add_executable(nexigen_exec_logging_tests exec/execution_manager_logging_test.cpp)
target_link_libraries(nexigen_exec_logging_tests PRIVATE nexigen_exec Threads::Threads)

add_test(NAME nexigen_exec_logging_tests COMMAND nexigen_exec_logging_tests)
set_tests_properties(nexigen_exec_logging_tests PROPERTIES TIMEOUT 300)

# Юнит-тесты nexigen::com — словарь ошибок, имена сервисов, Node и концепт
# Message<T>. Только при NEXIGEN_ENABLE_COM=ON (зависимость от iceoryx2).
if(NEXIGEN_ENABLE_COM)
    add_executable(nexigen_com_tests com/com_test.cpp)
    target_link_libraries(nexigen_com_tests PRIVATE nexigen_com Threads::Threads)

    add_test(NAME nexigen_com_tests COMMAND nexigen_com_tests)
    set_tests_properties(nexigen_com_tests PROPERTIES TIMEOUT 600)

    # Микробенч латентности горячего пути логгера поверх iceoryx2
    # (change bench-log-iceoryx2-latency): печатает медиану/p99/p99.9/max,
    # строгий sanity-гейт (a+event <= 2 мкс) только в Release x86-64.
    add_executable(nexigen_com_latency_tests com/latency_test.cpp)
    target_link_libraries(nexigen_com_latency_tests PRIVATE nexigen_com Threads::Threads)
    if(NEXIGEN_STRICT_LATENCY_TESTS)
        target_compile_definitions(nexigen_com_latency_tests PRIVATE
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:NEXIGEN_COM_STRICT_LATENCY>)
    endif()

    add_test(NAME nexigen_com_latency_tests COMMAND nexigen_com_latency_tests)
    set_tests_properties(nexigen_com_latency_tests PROPERTIES TIMEOUT 600)

    # Юнит-тесты nexigen::phm — health channel, supervision, recovery, manifest.
    add_executable(nexigen_phm_tests phm/phm_test.cpp)
    target_link_libraries(nexigen_phm_tests PRIVATE nexigen_phm Threads::Threads)

    add_test(NAME nexigen_phm_tests COMMAND nexigen_phm_tests)
    set_tests_properties(nexigen_phm_tests PROPERTIES TIMEOUT 600)

    # Юнит-тесты nexigen::sm — state machine, arbitration, ipc, recovery, telemetry.
    add_executable(nexigen_sm_tests sm/sm_test.cpp)
    target_link_libraries(nexigen_sm_tests PRIVATE nexigen_sm nexigen_phm Threads::Threads)

    add_test(NAME nexigen_sm_tests COMMAND nexigen_sm_tests)
    set_tests_properties(nexigen_sm_tests PROPERTIES TIMEOUT 600)

    # Юнит-тесты nexigen::exec::StateClient (change add-sm).
    add_executable(nexigen_state_client_tests exec/state_client_test.cpp)
    target_link_libraries(nexigen_state_client_tests PRIVATE
        nexigen_state_client nexigen_com Threads::Threads)

    add_test(NAME nexigen_state_client_tests COMMAND nexigen_state_client_tests)
    set_tests_properties(nexigen_state_client_tests PROPERTIES TIMEOUT 600)

    # Юнит-тесты оркестрации EM (change add-em-orchestration).
    add_executable(nexigen_em_orchestration_tests exec/em_orchestration_test.cpp)
    target_link_libraries(nexigen_em_orchestration_tests PRIVATE
        nexigen_state_client nexigen_com Threads::Threads)

    add_test(NAME nexigen_em_orchestration_tests COMMAND nexigen_em_orchestration_tests)
    set_tests_properties(nexigen_em_orchestration_tests PROPERTIES TIMEOUT 600)

    # Юнит-тесты nexigen::logging::Iceoryx2Logger (change add-iceoryx2-logger).
    add_executable(nexigen_logging_com_tests logging/iceoryx2_logger_test.cpp)
    target_link_libraries(nexigen_logging_com_tests PRIVATE
        nexigen_logging_com Threads::Threads)

    add_test(NAME nexigen_logging_com_tests COMMAND nexigen_logging_com_tests)
    set_tests_properties(nexigen_logging_com_tests PROPERTIES TIMEOUT 600)

    # Negative-compile probe: non-trivially-copyable payloads (owning string,
    # virtuals, containers) must be rejected by concepts::Message at compile
    # time. Тот же механизм, что tests/core/negative_compile.sh.
    get_target_property(_nexigen_com_iox2_includes iceoryx2-cxx::static-lib-cxx
        INTERFACE_INCLUDE_DIRECTORIES)
    set(_nexigen_com_iox2_inc_flags "")
    foreach(_nexigen_com_inc IN LISTS _nexigen_com_iox2_includes)
        list(APPEND _nexigen_com_iox2_inc_flags "-I${_nexigen_com_inc}")
    endforeach()
    add_test(NAME nexigen_com_message_negative_compile
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/core/negative_compile.sh
            ${CMAKE_CXX_COMPILER}
            ${CMAKE_CURRENT_SOURCE_DIR}/com/neg/message_non_pod_negative.cpp
            ${PROJECT_SOURCE_DIR}/include
            -frtti
            ${_nexigen_com_iox2_inc_flags})
    set_tests_properties(nexigen_com_message_negative_compile PROPERTIES TIMEOUT 300)
    unset(_nexigen_com_inc)
    unset(_nexigen_com_iox2_includes)
    unset(_nexigen_com_iox2_inc_flags)
endif()

if(NEXIGEN_BUILD_TOOLS)
    # The CLI tests drive the real nexigen-exec/nexigen-hub binaries, which
    # exist only when tools are built; tests=ON + tools=OFF must configure
    # and run the remaining suites (analysis 2026-11-03 #20).
    add_test(NAME nexigen_run_cli_tests
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/exec/cli_test.sh
                     $<TARGET_FILE:nexigen-exec>)
    set_tests_properties(nexigen_run_cli_tests PROPERTIES TIMEOUT 300)
endif()

add_executable(nexigen_hub_tests
    hub/main.cpp
    hub/model_test.cpp
    hub/discovery_test.cpp
    hub/streaming_test.cpp
    hub/web_assets_test.cpp
    hub/http_server_test.cpp)
target_link_libraries(nexigen_hub_tests PRIVATE nexigen_hub Threads::Threads)

add_test(NAME nexigen_hub_tests COMMAND nexigen_hub_tests)
set_tests_properties(nexigen_hub_tests PROPERTIES TIMEOUT 600)

if(NEXIGEN_BUILD_TOOLS)
    add_test(NAME nexigen_hub_cli_tests
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/hub/hub_cli_test.sh
                     $<TARGET_FILE:nexigen-hub>)
    set_tests_properties(nexigen_hub_cli_tests PROPERTIES TIMEOUT 120)
endif()

# =============================================================================
# speedup-validation-loop 1.3: PCH-прелюд и тестовым исполняемым (тот же
# compiled state, что у библиотек: verified-равные дефайны, design D2).
# if(TARGET)-guard переживает включение/выключение опциональных тестов.
# =============================================================================
include(Pch)
foreach(_nexigen_test
        nexigen_core_tests
        nexigen_chrono_tests
        nexigen_logging_tests
        nexigen_metrics_tests
        nexigen_tracing_tests
        nexigen_diagnostics_tests
        nexigen_app_tests
        nexigen_exec_tests
        nexigen_app_suite_tests
        nexigen_child_process_tests
        nexigen_exec_logging_tests
        nexigen_com_tests
        nexigen_com_latency_tests
        nexigen_phm_tests
        nexigen_sm_tests
        nexigen_state_client_tests
        nexigen_em_orchestration_tests
        nexigen_logging_com_tests
        nexigen_hub_tests)
    if(TARGET ${_nexigen_test})
        nexigen_precompile(${_nexigen_test})
    endif()
endforeach()
unset(_nexigen_test)
