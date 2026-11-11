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
include(Format)
include(Complexity)

# speedup-validation-loop 1.4 (design D5): прозрачный компайл-кэш, когда
# ccache лежит на PATH; отсутствие — no-op (установка не требуется).
# Экспорт безопасен: CMAKE_*_COMPILER_LAUNCHER — переменная build-tree,
# в nexigenConfig.cmake не попадает.
find_program(NEXIGEN_CCACHE_PROGRAM NAMES ccache)
if(NEXIGEN_CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${NEXIGEN_CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${NEXIGEN_CCACHE_PROGRAM}")
    message(STATUS "🧰 compile cache: ccache (${NEXIGEN_CCACHE_PROGRAM})")
else()
    message(STATUS "🧰 compile cache: ccache not on PATH (conda install -c conda-forge ccache)")
endif()

# Опция покрытия: инструментирует ВСЕ цели и добавляет target `coverage`.
option(NEXIGEN_ENABLE_COVERAGE "Instrument the build with GCC/Clang --coverage" OFF)
set(NEXIGEN_COVERAGE_FAIL_UNDER "0" CACHE STRING
    "Fail the coverage target below this line-coverage percentage (0 = no gate)")

# Test-only cold-path fault injection (Debug/coverage CI). The checks compile
# to constant false in production builds.
option(NEXIGEN_ENABLE_FAULT_INJECTION
    "Enable test-only fault injection hooks for cold-path syscalls" OFF)

# libFuzzer harnesses are Clang-only and not part of the default build.
option(NEXIGEN_BUILD_FUZZERS "Build libFuzzer harnesses (Clang only)" OFF)

# ns-class latency gates are meaningful only on dedicated hardware. Shared CI
# runners and containers cannot guarantee the 20 ns budget, so Release CI can
# turn the gates off while still exercising optimized code.
option(NEXIGEN_STRICT_LATENCY_TESTS
    "Enable ns-class latency gates in Release/RelWithDebInfo tests" ON)

# Portable distributions must not assume the build host's instruction set.
# The default keeps the native tuning used on dedicated HIL hardware.
option(NEXIGEN_NATIVE_ARCH
    "Use -march=native/-mtune=native in Release/RelWithDebInfo builds" ON)

# nexigen::com — SOA over iceoryx2 (change add-com-module). The dependency is
# Rust-based (cargo -> iceoryx2-ffi-c -> iceoryx2-bb-cxx -> iceoryx2-cxx), so
# the module and the whole Rust build chain sit behind this gate; OFF keeps
# every other target free of any Rust reference.
option(NEXIGEN_ENABLE_COM
    "Build nexigen::com (SOA over iceoryx2; requires the Rust toolchain)" OFF)

if(NEXIGEN_ENABLE_COVERAGE AND (NEXIGEN_ENABLE_ASAN OR NEXIGEN_ENABLE_TSAN))
    message(FATAL_ERROR "❌ Coverage cannot be combined with ASan/TSan in one build.")
endif()

# Создаем единую интерфейсную цель с правилами компиляции
add_library(nexigen_options INTERFACE)

include(GNUInstallDirs)

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
    # Forked execution manager children flush gcov counters before `_exit()`/`execvp()`.
    target_compile_definitions(nexigen_options INTERFACE
        NEXIGEN_ENABLE_COVERAGE=1)
endif()

if(NEXIGEN_ENABLE_FAULT_INJECTION)
    message(STATUS "🧪 Cold-path fault injection enabled")
    target_compile_definitions(nexigen_options INTERFACE
        NEXIGEN_ENABLE_FAULT_INJECTION=1)
endif()

# Настраиваем пути к заголовочным файлам
target_include_directories(nexigen_options INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# =============================================================================
# nlohmann::json — vocabularies to_json/from_json для enum-контрактов
# (design D1 change add-serialization-and-wait-free-logging). Мандат 12
# (openspec/config.yaml): только nlohmann/json ровно v3.12.0 — системный
# пакет принимается лишь при EXACT-совпадении версии, любая другая версия
# уходит в закреплённый FetchContent-архив.
# JSON_NOEXCEPTION=1 определяется PUBLIC (INTERFACE): каждый consumer-TU
# обязан инклудить <nlohmann/json.hpp> при -fno-exceptions, иначе заголовки
# генерируют try/catch и падают на строгом флаге.
# =============================================================================
find_package(nlohmann_json 3.12.0 EXACT QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    FetchContent_Declare(nlohmann_json
        SYSTEM
        URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
        URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# =============================================================================
# iceoryx2 — zero-copy IPC backend for nexigen::com (change add-com-module).
# Пин: коммит b0660b5b3574d3cc18911f3c115e270630120b75. В репозитории нет
# тегов и GitHub-релизов; этот коммит — main на версии 0.10.999-dev, сразу
# после стабильного 0.10.0 (crates.io, 2026-09-18). Сборка вызывает cargo
# (Rust) — поэтому всё за гейтом NEXIGEN_ENABLE_COM.
# =============================================================================
if(NEXIGEN_ENABLE_COM)
    include(FetchContent)
    # Мандат проекта — std::expected: переводим биндинг с iox2-expected на
    # стандартный. Примеры/тесты iceoryx2 не собираем (BUILD_EXAMPLES OFF по
    # умолчанию — фиксируем явно).
    set(IOX2_BB_CXX_CONFIG_USE_STD_EXPECTED ON CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(iceoryx2
        SYSTEM
        GIT_REPOSITORY https://github.com/ekxide/iceoryx2.git
        GIT_TAG b0660b5b3574d3cc18911f3c115e270630120b75
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(iceoryx2)
endif()

# =============================================================================
# nexigen::core — header-only библиотека.
# Публичные заголовки лежат в include/, компиляции нет: флаги и стандарт
# наследуются от nexigen_options (единственный источник настроек компилятора).
# Threads ищем ДО определения целей (change dedup-affinity-core): core-интер-
# фейс линкует Threads::Threads для affinity-хедера; экспорт безопасен —
# nexigenConfig.cmake.in уже делает find_dependency(Threads).
# =============================================================================
find_package(Threads REQUIRED)

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
target_link_libraries(nexigen_core INTERFACE
    # Build-tree only: installed consumers must not inherit -Werror/LTO/
    # sanitizer flags. nlohmann_json is re-attached by nexigenConfig.cmake
    # after find_dependency, because a FetchContent build cannot export an
    # external target.
    $<BUILD_INTERFACE:nexigen_options>
    $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>
    # pthread_*affinity_np in core/affinity.hpp (dedup-affinity-core D2):
    # link-level dependency on the INTERFACE target so core-only consumers
    # link without per-target wiring.
    Threads::Threads)
target_compile_definitions(nexigen_core INTERFACE JSON_NOEXCEPTION=1)

# clock_gettime()/CLOCK_MONOTONIC_RAW на Linux: явно запрашиваем POSIX-объявления,
# чтобы строгий -std=c++23 (без _GNU_SOURCE) их не скрывал. См. design.md D2
# (nexigen::chrono/timestamp.hpp наследует это определение через nexigen_core).
target_compile_definitions(nexigen_core INTERFACE $<$<PLATFORM_ID:Linux>:_POSIX_C_SOURCE=200809L>)
# core/affinity.hpp: glibc-extensions pthread_*affinity_np (dedup-affinity-core
# D2); module-level defines (ниже) остаются — повторный #define безвреден.
target_compile_definitions(nexigen_core INTERFACE $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)

# =============================================================================
# nexigen::chrono — time primitives экосистемы: монотонный Timestamp, калиб-
# рованный stamp source, UTC-форматирование и служба времени (ChronoProvider +
#  сенки + настройки). Публичные заголовки в include/, холодные юниты
#  (калибровка, сенки, сериализация, провайдер, загрузчик настроек) в
#  src/chrono/. Горячий путь остаётся inline в заголовках.
# _GNU_SOURCE нужен cpu_set_t/pthread_setaffinity_np; потоки — для фонового
# воркера службы (std::jthread).
# =============================================================================
find_package(Threads REQUIRED)

file(GLOB NEXIGEN_CHRONO_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/chrono/*.cpp")

add_library(nexigen_chrono STATIC ${NEXIGEN_CHRONO_SOURCES})
add_library(nexigen::chrono ALIAS nexigen_chrono)

target_include_directories(nexigen_chrono PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# Флаги/-Werror/санитайзеры, cxx_std_23 и _POSIX_C_SOURCE приходят транзитивно
# от nexigen_core (INTERFACE -> nexigen_options): chrono живёт на error-модели
# core (Result/Status). _GNU_SOURCE — для аффинности воркера; Threads — для
# фонового std::jthread службы.
target_compile_definitions(nexigen_chrono PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
target_link_libraries(nexigen_chrono PUBLIC nexigen_core PRIVATE Threads::Threads)

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
# nexigen::exec — instance lifecycle library.
# =============================================================================
file(GLOB NEXIGEN_EXEC_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/exec/*.cpp")
# state_client.cpp depends on nexigen::com and lives in the gated
# nexigen_state_client target (change add-sm); it must never enter nexigen_exec,
# which builds with the gate OFF.
list(FILTER NEXIGEN_EXEC_SOURCES EXCLUDE REGEX "state_client\\.cpp$")

add_library(nexigen_exec STATIC ${NEXIGEN_EXEC_SOURCES})
add_library(nexigen::exec ALIAS nexigen_exec)

target_include_directories(nexigen_exec PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(nexigen_exec PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
target_link_libraries(nexigen_exec PUBLIC nexigen_core nexigen_app PRIVATE Threads::Threads)

# =============================================================================
# nexigen::hub — read-only observatory library.
# =============================================================================
file(GLOB NEXIGEN_HUB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hub/*.cpp")

add_library(nexigen_hub STATIC ${NEXIGEN_HUB_SOURCES})
add_library(nexigen::hub ALIAS nexigen_hub)

target_include_directories(nexigen_hub PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(nexigen_hub PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
target_link_libraries(nexigen_hub PUBLIC nexigen_core nexigen_exec PRIVATE Threads::Threads)

# =============================================================================
# nexigen::com — SOA over iceoryx2 (change add-com-module). Собирается только
# за гейтом NEXIGEN_ENABLE_COM: тонкая обёртка над iceoryx2-cxx (iox2::) с
# собственным com::Error и zero-copy паттернами (pub-sub, event, method, field).
# =============================================================================
if(NEXIGEN_ENABLE_COM)
    file(GLOB NEXIGEN_COM_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/com/*.cpp")

    add_library(nexigen_com STATIC ${NEXIGEN_COM_SOURCES})
    add_library(nexigen::com ALIAS nexigen_com)

    target_include_directories(nexigen_com PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(nexigen_com PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
    target_link_libraries(nexigen_com PUBLIC nexigen_core iceoryx2-cxx::static-lib-cxx)

    # iceoryx2-cxx derives payload type names via typeid (its default
    # type-name mechanism, exercised only at port creation — cold path), so
    # the com module opts back into RTTI; every other nexigen module keeps the
    # global -fno-rtti mandate (deviation recorded in design D9). The -frtti
    # rides on the iceoryx2 target's INTERFACE, which is linked after
    # nexigen_core, so it wins over the inherited -fno-rtti (GCC: last flag
    # wins).
    target_compile_options(static-lib-cxx INTERFACE -frtti)

    # nexigen::phm — Platform Health Management (change add-phm): клиентская
    # библиотека (health channel, checkpoint emission) и движки supervision /
    # recovery / fault storage поверх nexigen::com (Event, WaitSet,
    # Request-Response).
    file(GLOB NEXIGEN_PHM_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/phm/*.cpp")

    add_library(nexigen_phm STATIC ${NEXIGEN_PHM_SOURCES})
    add_library(nexigen::phm ALIAS nexigen_phm)

    target_include_directories(nexigen_phm PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(nexigen_phm PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
    target_link_libraries(nexigen_phm PUBLIC nexigen_com PRIVATE Threads::Threads)

    # nexigen::sm — State Management (change add-sm): центральный арбитр
    # состояний поверх nexigen::com (WaitSet, Field, Request-Response, Event).
    file(GLOB NEXIGEN_SM_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/sm/*.cpp")

    add_library(nexigen_sm STATIC ${NEXIGEN_SM_SOURCES})
    add_library(nexigen::sm ALIAS nexigen_sm)

    target_include_directories(nexigen_sm PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(nexigen_sm PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
    target_link_libraries(nexigen_sm PUBLIC nexigen_com PRIVATE Threads::Threads)

    # nexigen::exec::StateClient (change add-sm): приёмная сторона SetState в
    # EM. Зависит от com, поэтому отдельный gated-таргет; линкуется к
    # nexigen-exec при NEXIGEN_ENABLE_COM=ON.
    add_library(nexigen_state_client STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/exec/state_client.cpp")
    target_include_directories(nexigen_state_client PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(nexigen_state_client PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)
    target_link_libraries(nexigen_state_client PUBLIC nexigen_exec nexigen_com)

    # nexigen::logging_com — Iceoryx2Logger (change add-iceoryx2-logger):
    # header-only logger поверх nexigen::com. Гейт отдельным INTERFACE-таргетом,
    # ядро nexigen_logging остаётся свободным от iceoryx2.
    add_library(nexigen_logging_com INTERFACE)
    add_library(nexigen::logging_com ALIAS nexigen_logging_com)
    target_include_directories(nexigen_logging_com INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(nexigen_logging_com INTERFACE nexigen_logging nexigen_com)

    # nexigen::metrics_com — Iceoryx2MetricSink (change add-metrics-daemon):
    # header-only sink поверх nexigen::com. Гейт отдельным INTERFACE-таргетом,
    # ядро nexigen_metrics остаётся свободным от iceoryx2.
    add_library(nexigen_metrics_com INTERFACE)
    add_library(nexigen::metrics_com ALIAS nexigen_metrics_com)
    target_include_directories(nexigen_metrics_com INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(nexigen_metrics_com INTERFACE nexigen_metrics nexigen_com)
endif()

# =============================================================================
# speedup-validation-loop 1.2: один PCH-прелюд (include/nexigen/core/pch.hpp)
# на каждую статичную библиотеку — nlohmann/json и холодные std-хедеры
# парсятся один раз на таргет вместо каждой TU. Снапшот PCH идентичен
# текущему прелюду каждой TU (те же дефайны и прагмы, design D2); точка
# приложения — после всех add_library выше; тестовые/example-цели получают
# свой PCH отдельно (задача 1.3).
# =============================================================================
include(Pch)
nexigen_precompile(nexigen_chrono)
nexigen_precompile(nexigen_logging)
nexigen_precompile(nexigen_metrics)
nexigen_precompile(nexigen_tracing)
nexigen_precompile(nexigen_diagnostics)
nexigen_precompile(nexigen_app)
nexigen_precompile(nexigen_exec)
nexigen_precompile(nexigen_hub)
if(NEXIGEN_ENABLE_COM)
    nexigen_precompile(nexigen_com)
    nexigen_precompile(nexigen_phm)
    nexigen_precompile(nexigen_sm)
    nexigen_precompile(nexigen_state_client)
endif()

# =============================================================================
# Опции сборки: юнит-тесты и runnable-примеры (включены по умолчанию).
# enable_testing() поднимаем на уровень обеих веток: ctest должен видеть
# примеры даже при -DNEXIGEN_BUILD_TESTS=OFF.
# =============================================================================
option(NEXIGEN_BUILD_TESTS "Build nexigen unit tests" ON)
option(NEXIGEN_BUILD_EXAMPLES "Build nexigen runnable examples" ON)
option(NEXIGEN_BUILD_TOOLS "Build nexigen command-line tools" ON)

if(NEXIGEN_BUILD_TESTS OR NEXIGEN_BUILD_EXAMPLES)
    enable_testing()
endif()

if(NEXIGEN_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(NEXIGEN_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(NEXIGEN_BUILD_TOOLS)
    add_subdirectory(tools)
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
                --output-on-failure -R "^nexigen_.*_tests$" -E "_cli_tests$"
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/coverage_report.py
                --build-dir ${CMAKE_BINARY_DIR}
                --source-dir ${CMAKE_CURRENT_SOURCE_DIR}
                --gcov ${GCOV_EXECUTABLE}
                --fail-under ${NEXIGEN_COVERAGE_FAIL_UNDER}
                --module-fail-under exec=90
                --module-fail-under hub=90
        DEPENDS
            nexigen_core_tests
            nexigen_chrono_tests
            nexigen_logging_tests
            nexigen_metrics_tests
            nexigen_tracing_tests
            nexigen_diagnostics_tests
            nexigen_app_tests
            nexigen_exec_tests
            nexigen_app_suite_tests
            nexigen_exec_logging_tests
            nexigen_hub_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        VERBATIM
        COMMENT "Running unit tests under coverage and aggregating gcov data")
endif()

# =============================================================================
# Optional libFuzzer harnesses (Clang only, not part of the default build).
# =============================================================================
if(NEXIGEN_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "❌ NEXIGEN_BUILD_FUZZERS requires Clang.")
    endif()
    add_executable(nexigen_http_parse_fuzz tests/fuzz/http_parse_fuzz.cpp)
    target_link_libraries(nexigen_http_parse_fuzz PRIVATE nexigen_options)
    target_compile_options(nexigen_http_parse_fuzz PRIVATE
        -fsanitize=fuzzer,address -g -O1)
    target_link_options(nexigen_http_parse_fuzz PRIVATE
        -fsanitize=fuzzer,address)
endif()

# =============================================================================
# Install, export and package configuration.
#
# The export set exposes the in-tree alias names (nexigen::core, ...) and keeps
# build-only flags out of consumer targets: nexigen_options is attached through
# $<BUILD_INTERFACE:...>, so the installed nexigen::core carries only
# cxx_std_23, JSON_NOEXCEPTION and the public platform definitions.
# =============================================================================
include(CMakePackageConfigHelpers)

set(NEXIGEN_EXPORT_TARGETS
    nexigen_core
    nexigen_chrono
    nexigen_logging
    nexigen_metrics
    nexigen_tracing
    nexigen_diagnostics
    nexigen_app
    nexigen_exec
    nexigen_hub)
if(NEXIGEN_ENABLE_COM)
    list(APPEND NEXIGEN_EXPORT_TARGETS nexigen_com nexigen_phm nexigen_sm)
endif()

set_target_properties(nexigen_core        PROPERTIES EXPORT_NAME core)
set_target_properties(nexigen_chrono      PROPERTIES EXPORT_NAME chrono)
set_target_properties(nexigen_logging     PROPERTIES EXPORT_NAME logging)
set_target_properties(nexigen_metrics     PROPERTIES EXPORT_NAME metrics)
set_target_properties(nexigen_tracing     PROPERTIES EXPORT_NAME tracing)
set_target_properties(nexigen_diagnostics PROPERTIES EXPORT_NAME diagnostics)
set_target_properties(nexigen_app         PROPERTIES EXPORT_NAME app)
set_target_properties(nexigen_exec      PROPERTIES EXPORT_NAME exec)
set_target_properties(nexigen_hub         PROPERTIES EXPORT_NAME hub)
if(NEXIGEN_ENABLE_COM)
    set_target_properties(nexigen_com     PROPERTIES EXPORT_NAME com)
    set_target_properties(nexigen_phm     PROPERTIES EXPORT_NAME phm)
    set_target_properties(nexigen_sm      PROPERTIES EXPORT_NAME sm)
endif()

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/nexigen"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(TARGETS ${NEXIGEN_EXPORT_TARGETS}
        EXPORT nexigenTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(EXPORT nexigenTargets
        FILE nexigenTargets.cmake
        NAMESPACE nexigen::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/nexigen")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/nexigenConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/nexigenConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/nexigen")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/nexigenConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/nexigenConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/nexigenConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/nexigen")

message(STATUS "⚡ nexigen-cpp configured [Build type: ${CMAKE_BUILD_TYPE}]")
