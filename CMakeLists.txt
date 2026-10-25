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

# Создаем единую интерфейсную цель с правилами компиляции
add_library(nexigen_options INTERFACE)

# Применяем функции из наших скриптов
nexigen_set_warnings(nexigen_options)
nexigen_set_optimizations(nexigen_options)
nexigen_set_sanitizers(nexigen_options)

# Настраиваем пути к заголовочным файлам
target_include_directories(nexigen_options INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

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
target_link_libraries(nexigen_core INTERFACE nexigen_options)

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
# Юнит-тесты (по умолчанию включены; отключаются -DNEXIGEN_BUILD_TESTS=OFF)
# =============================================================================
option(NEXIGEN_BUILD_TESTS "Build nexigen unit tests" ON)

if(NEXIGEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "⚡ nexigen-cpp configured [Build type: ${CMAKE_BUILD_TYPE}]")
