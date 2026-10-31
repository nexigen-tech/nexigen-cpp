cmake_minimum_required(VERSION 3.25)
project(nexigen-cpp VERSION 0.1.0 LANGUAGES CXX)

# Строгий стандарт C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Генерируем compile_commands.json (нужен для умного автодополнения в IDE и для OpenCode)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Подключаем наши скрипты из папки cmake/
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Optimizations)
include(Sanitizers)

# Опция покрытия: инструментирует ВСЕ цели и добавляет target `coverage`.
option(NEXIGEN_ENABLE_COVERAGE "Instrument the build with GCC/Clang --coverage" OFF)
set(NEXIGEN_COVERAGE_FAIL_UNDER "0" CACHE STRING
    "Fail the coverage target below this line-coverage percentage (0 = no gate)")

if(NEXIGEN_ENABLE_COVERAGE AND (NEXIGEN_ENABLE_ASAN OR NEXIGEN_ENABLE_TSAN))
    message(FATAL_ERROR "❌ Coverage cannot be combined with ASan/TSan in one build.")
endif()

# Создаем единую интерфейсную цель с правилами компиляции
add_library(nexigen_options INTERFACE)

# Применяем функции из наших скриптов
nexigen_set_warnings(nexigen_options)
nexigen_set_optimizations(nexigen_options)
nexigen_set_sanitizers(nexigen_options)

if(NEXIGEN_ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "❌ NEXIGEN_ENABLE_COVERAGE requires GCC or Clang.")
    endif()
    message(STATUS "📊 Coverage instrumentation enabled (--coverage)")
    target_compile_options(nexigen_options INTERFACE --coverage)
    target_link_options(nexigen_options INTERFACE --coverage)
endif()

# Настраиваем пути к заголовочным файлам
target_include_directories(nexigen_options INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# =============================================================================
# nlohmann::json — vocabularies to_json/from_json для enum-контрактов
# (design D1 change add-serialization-and-wait-free-logging). Предпочитаем
# системный пакет; иначе FetchContent (header-only, тег закреплён).
# JSON_NOEXCEPTION=1 определяется PUBLIC (INTERFACE): каждый consumer-TU
# обязан инклудить <nlohmann/json.hpp> при -fno-exceptions, иначе заголовки
# генерируют try/catch и падают на строгом флаге.
# =============================================================================
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    FetchContent_Declare(nlohmann_json
        SYSTEM
        URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# =============================================================================
# nexigen::core — header-only библиотека.
# Публичные заголовки лежат в include/, компиляции нет: флаги и стандарт
# наследуются от nexigen_options (единственный источник настроек компилятора).
# =============================================================================
add_library(nexigen_core INTERFACE)
add_library(nexigen::core ALIAS nexigen_core)

target_include_directories(nexigen_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(nexigen_core INTERFACE cxx_std_23)
# nlohmann_json + JSON_NOEXCEPTION — INTERFACE: транзитивно на chrono/logging/
# tests/downstream, чтобы enum-заголовки (result.hpp и др.) компилировались
# везде под общим -fno-exceptions (design D1).
target_link_libraries(nexigen_core INTERFACE nexigen_options nlohmann_json::nlohmann_json)
target_compile_definitions(nexigen_core INTERFACE JSON_NOEXCEPTION=1)

# clock_gettime()/CLOCK_MONOTONIC_RAW на Linux: явно запрашиваем POSIX-объявления,
# чтобы строгий -std=c++23 (без _GNU_SOURCE) их не скрывал. См. design.md D2
# (nexigen::chrono/timestamp.hpp наследует это определение через nexigen_core).
target_compile_definitions(nexigen_core INTERFACE $<$<PLATFORM_ID:Linux>:_POSIX_C_SOURCE=200809L>)

# =============================================================================
# nexigen::chrono — time primitives экосистемы: монотонный Timestamp, калиб-
# рованный stamp source, UTC-форматирование. Публичные заголовки в include/,
# холодные юниты (калибровка со сном, civil-time форматирование) компилируются
# в src/chrono/. Горячий путь остаётся inline в заголовках.
# =============================================================================
file(GLOB NEXIGEN_CHRONO_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/chrono/*.cpp")

add_library(nexigen_chrono STATIC ${NEXIGEN_CHRONO_SOURCES})
add_library(nexigen::chrono ALIAS nexigen_chrono)

target_include_directories(nexigen_chrono PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# Флаги/-Werror/санитайзеры, cxx_std_23 и _POSIX_C_SOURCE приходят транзитивно
# от nexigen_core (INTERFACE -> nexigen_options): chrono живёт на error-модели
# core (Result/Status), своих компиляторных настроек у него нет.
target_link_libraries(nexigen_chrono PUBLIC nexigen_core)

# =============================================================================
# nexigen::logging — компилируемая библиотека: публичные заголовки в include/,
# холодные потребительские юниты (JSON-формат, аффинность, ротация) в
# src/logging/ — форматирование времени и калибровка вынесены в nexigen::chrono.
# Горячий путь остаётся inline в заголовках (design D1).
# _GNU_SOURCE нужен cpu_set_t/pthread_setaffinity_np/syscall(SYS_gettid) в
# строгом -std=c++23; он расширяет (не сужает) _POSIX_C_SOURCE от nexigen_core.
# =============================================================================
find_package(Threads REQUIRED)

file(GLOB NEXIGEN_LOGGING_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/logging/*.cpp")

add_library(nexigen_logging STATIC ${NEXIGEN_LOGGING_SOURCES})
add_library(nexigen::logging ALIAS nexigen_logging)

target_include_directories(nexigen_logging PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(nexigen_logging PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
# Флаги/-Werror/санитайзеры и cxx_std_23 приходят транзитивно от nexigen_core
# (её INTERFACE -> nexigen_options); потоки нужны только для холодных юнитов.
# nexigen_chrono — PUBLIC: LogRecord несёт chrono::Timestamp в публичных
# заголовках, downstream-юниты компилируют и линкуют chrono транзитивно.
target_link_libraries(nexigen_logging PUBLIC nexigen_core nexigen_chrono PRIVATE Threads::Threads)

# =============================================================================
# nexigen::metrics — измерительный слой: инструменты нулевых аллокаций и
# wait-free обновлений, фоновый сбор с привязкой к CPU. Публичные заголовки
# в include/, холодные юниты (провайдер, сенки, сериализация) в src/metrics/;
# горячие пути остаются inline в заголовках (design D1).
# _GNU_SOURCE нужен cpu_set_t/pthread_setaffinity_np в строгом -std=c++23.
# =============================================================================
file(GLOB NEXIGEN_METRICS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/*.cpp")

add_library(nexigen_metrics STATIC ${NEXIGEN_METRICS_SOURCES})
add_library(nexigen::metrics ALIAS nexigen_metrics)

target_include_directories(nexigen_metrics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(nexigen_metrics PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
# Флаги/-Werror/санитайзеры и cxx_std_23 приходят транзитивно от nexigen_core
# (её INTERFACE -> nexigen_options); nexigen_chrono — PUBLIC: снапшоты и
# JSON-сенк несут chrono::Timestamp в публичных заголовках. Потоки нужны для
# фонового сборщика (std::jthread).
target_link_libraries(nexigen_metrics PUBLIC nexigen_core nexigen_chrono PRIVATE Threads::Threads)

# =============================================================================
# nexigen::tracing — подсистема причинно-следственной телеметрии: SPAN-записи
# с автоматической/сквозной родительской связью, wait-free шардирование по
# потокам и фоновый потребитель с привязкой к CPU. Публичные заголовки в
# include/, холодные юниты (провайдер, сенки, сериализация) в src/tracing/;
# горячие пути (start_span/finish_span) остаются inline в заголовках (design D2).
# _GNU_SOURCE нужен cpu_set_t/pthread_setaffinity_np/SYS_gettid в строгом
# -std=c++23; он расширяет (не сужает) _POSIX_C_SOURCE от nexigen_core.
# =============================================================================
file(GLOB NEXIGEN_TRACING_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/tracing/*.cpp")

# Скаффолдинг: на первом шаге (пустое дерево src/tracing/) CMake 4 не даёт
# создать STATIC-цель без источников. Генерируем нейтральный placeholder-TU
# в бинарнике; первый настоящий src/tracing/*.cpp забирает glob, и условие
# исчезает само (CONFIGURE_DEPENDS перезапускает конфигурацию).
if(NOT NEXIGEN_TRACING_SOURCES)
    set(NEXIGEN_TRACING_PLACEHOLDER "${CMAKE_CURRENT_BINARY_DIR}/tracing_scaffold.cpp")
    file(WRITE "${NEXIGEN_TRACING_PLACEHOLDER}"
"// nexigen::tracing scaffolding placeholder: exists only while src/tracing/\n"
"// holds no real cold-path units; removed from the build automatically once\n"
"// the first src/tracing/*.cpp lands (glob is CONFIGURE_DEPENDS).\n"
"namespace nexigen::tracing { extern const bool scaffolded = true; }\n")
    list(APPEND NEXIGEN_TRACING_SOURCES "${NEXIGEN_TRACING_PLACEHOLDER}")
endif()

add_library(nexigen_tracing STATIC ${NEXIGEN_TRACING_SOURCES})
add_library(nexigen::tracing ALIAS nexigen_tracing)

target_include_directories(nexigen_tracing PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(nexigen_tracing PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
# Флаги/-Werror/санитайзеры и cxx_std_23 приходят транзитивно от nexigen_core
# (её INTERFACE -> nexigen_options); nexigen_chrono — PUBLIC: SpanRecord несёт
# chrono::Timestamp в публичных заголовках, а провайдер калибрует Stamp_source.
# Потоки нужны для холодного потребителя (std::jthread) и аффинности.
target_link_libraries(nexigen_tracing PUBLIC nexigen_core nexigen_chrono PRIVATE Threads::Threads)

# =============================================================================
# nexigen::diagnostics — измерительный слой времени: фиксированный в памяти
# интервальный таймер Stopwatch (такты из nexigen::chrono — Timestamp::now()
# или откалиброванный Stamp_source за ~3-5 ns) и RAII-страж ScopedStopwatch.
# Публичные заголовки в include/, холодный юнит рендеринга диагностики в
# src/diagnostics/; горячие пути (старты/стопы/редо/круги/запросы) остаются
# inline в заголовках. Потоки и аффинность не нужны: домен измерения — один
# поток, без фонового потребителя (design D1).
# =============================================================================
file(GLOB NEXIGEN_DIAGNOSTICS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics/*.cpp")

# Тот же скеффолдинг, что у tracing: пока src/diagnostics/ пуст, CMake 4 не
# даёт создать STATIC-цель без источников. Первый настоящий src/diagnostics/
# *.cpp забирается glob-ом, и условие исчезает само (CONFIGURE_DEPENDS
# перезапускает конфигурацию).
if(NOT NEXIGEN_DIAGNOSTICS_SOURCES)
    set(NEXIGEN_DIAGNOSTICS_PLACEHOLDER "${CMAKE_CURRENT_BINARY_DIR}/diagnostics_scaffold.cpp")
    file(WRITE "${NEXIGEN_DIAGNOSTICS_PLACEHOLDER}"
"// nexigen::diagnostics scaffolding placeholder: exists only while src/diagnostics/\n"
"// holds no real cold-path units; removed from the build automatically once\n"
"// the first src/diagnostics/*.cpp lands (glob is CONFIGURE_DEPENDS).\n"
"namespace nexigen::diagnostics { extern const bool scaffolded = true; }\n")
    list(APPEND NEXIGEN_DIAGNOSTICS_SOURCES "${NEXIGEN_DIAGNOSTICS_PLACEHOLDER}")
endif()

add_library(nexigen_diagnostics STATIC ${NEXIGEN_DIAGNOSTICS_SOURCES})
add_library(nexigen::diagnostics ALIAS nexigen_diagnostics)

target_include_directories(nexigen_diagnostics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# Флаги/-Werror/санитайзеры и cxx_std_23 приходят транзитивно от nexigen_core
# (её INTERFACE -> nexigen_options); nexigen_chrono — PUBLIC: Stopwatch несёт
# chrono::Stamp_source* в публичных заголовках, downstream компилирует и
# линкует chrono транзитивно. Потоки не нужны (см. комментарий к модулю).
target_link_libraries(nexigen_diagnostics PUBLIC nexigen_core nexigen_chrono)

# =============================================================================
# nexigen::app — прикладной слой: базовый lifecycle приложения, владение
# логгером/метриками/трассировкой, загрузка global/local настроек подсистем и
# обработка SIGINT/SIGTERM через signalfd. Публичные заголовки в include/,
# холодные юниты (загрузчики настроек) в src/app/; горячих путей нет — App
# вызывает пользовательский tick и только читает atomic stop-флаг.
# =============================================================================
file(GLOB NEXIGEN_APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp")

add_library(nexigen_app STATIC ${NEXIGEN_APP_SOURCES})
add_library(nexigen::app ALIAS nexigen_app)

target_include_directories(nexigen_app PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(nexigen_app PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
target_link_libraries(nexigen_app PUBLIC nexigen_core nexigen_chrono nexigen_logging
    nexigen_metrics nexigen_tracing PRIVATE Threads::Threads)

# =============================================================================
# Опции сборки: юнит-тесты и runnable-примеры (включены по умолчанию).
# enable_testing() поднимаем на уровень обеих веток: ctest должен видеть
# примеры даже при -DNEXIGEN_BUILD_TESTS=OFF.
# =============================================================================
option(NEXIGEN_BUILD_TESTS "Build nexigen unit tests" ON)
option(NEXIGEN_BUILD_EXAMPLES "Build nexigen runnable examples" ON)

if(NEXIGEN_BUILD_TESTS OR NEXIGEN_BUILD_EXAMPLES)
    enable_testing()
endif()

if(NEXIGEN_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(NEXIGEN_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# =============================================================================
# Target `coverage`: очищает счётчики, прогоняет unit-тесты и печатает
# line-coverage по модулям через gcov JSON. Требует NEXIGEN_ENABLE_COVERAGE=ON
# и NEXIGEN_BUILD_TESTS=ON; внешние gcovr/lcov не нужны.
# =============================================================================
if(NEXIGEN_ENABLE_COVERAGE)
    if(NOT NEXIGEN_BUILD_TESTS)
        message(FATAL_ERROR "❌ NEXIGEN_ENABLE_COVERAGE requires NEXIGEN_BUILD_TESTS=ON.")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(GCOV_EXECUTABLE gcov REQUIRED)

    add_custom_target(coverage
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/coverage_report.py
                --clean-only --build-dir ${CMAKE_BINARY_DIR}
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR}
                --output-on-failure -R "^nexigen_.*_tests$"
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/coverage_report.py
                --build-dir ${CMAKE_BINARY_DIR}
                --source-dir ${CMAKE_CURRENT_SOURCE_DIR}
                --gcov ${GCOV_EXECUTABLE}
                --fail-under ${NEXIGEN_COVERAGE_FAIL_UNDER}
        DEPENDS
            nexigen_core_tests
            nexigen_chrono_tests
            nexigen_logging_tests
            nexigen_metrics_tests
            nexigen_tracing_tests
            nexigen_diagnostics_tests
            nexigen_app_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        VERBATIM
        COMMENT "Running unit tests under coverage and aggregating gcov data")
endif()

message(STATUS "⚡ nexigen-cpp configured [Build type: ${CMAKE_BUILD_TYPE}]")
