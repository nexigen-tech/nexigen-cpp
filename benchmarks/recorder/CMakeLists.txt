# Opt-in storage-format benchmark for the recorder.
#
# This tree is deliberately outside include/, src/, and tools/: it must never
# become a production dependency or appear in the install/export set. The MCAP
# C++ library is header-only and has no CMakeLists, so FetchContent only
# populates the source tree and this file wires the include path itself.

include(FetchContent)

# CMake 4.x deprecates the single-argument FetchContent_Populate form. The
# benchmark needs it because the pinned MCAP repository has no top-level
# CMakeLists.txt (the C++ library is header-only). Keep the old policy local to
# this opt-in subtree.
cmake_policy(SET CMP0169 OLD)

FetchContent_Declare(mcap
    URL https://codeload.github.com/foxglove/mcap/tar.gz/42aea98d01dbbfe14c53f787378ee3d70ddb2e61
    URL_HASH SHA256=b4dbe68b88244365cc4627c5a88f6ed517e1ad4e5d5ed50236f80d3ade9d501e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_GetProperties(mcap)
if(NOT mcap_POPULATED)
    FetchContent_Populate(mcap)
endif()

add_library(nexigen_mcap_bench INTERFACE)
target_include_directories(nexigen_mcap_bench SYSTEM INTERFACE
    ${mcap_SOURCE_DIR}/cpp/mcap/include)
target_compile_definitions(nexigen_mcap_bench INTERFACE
    MCAP_COMPRESSION_NO_ZSTD
    MCAP_COMPRESSION_NO_LZ4)

# MCAP's writer.inl contains an explicit `throw std::system_error` in
# FileWriter. The project is built with -fno-exceptions, so the implementation
# TU is isolated in its own object library compiled with exceptions enabled and
# linked only into this benchmark. Production targets never see it.
add_library(nexigen_mcap_bench_impl OBJECT mcap_impl.cpp mcap_adapter.cpp)
target_link_libraries(nexigen_mcap_bench_impl PRIVATE
    nexigen_mcap_bench
    nlohmann_json::nlohmann_json
    Threads::Threads)
target_include_directories(nexigen_mcap_bench_impl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(nexigen_mcap_bench_impl PRIVATE
    JSON_NOEXCEPTION=1
    _GNU_SOURCE
    _POSIX_C_SOURCE=200809L)
target_compile_options(nexigen_mcap_bench_impl PRIVATE -fexceptions)

add_executable(nexigen_storage_bench
    main.cpp
    bench_types.cpp
    payload_blob.cpp
    metrics.cpp
    sink.cpp
    custom_writer.cpp
    video_writer.cpp)
target_link_libraries(nexigen_storage_bench PRIVATE
    nexigen_core
    nexigen_mcap_bench
    nexigen_mcap_bench_impl
    Threads::Threads)
target_include_directories(nexigen_storage_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(nexigen_storage_bench PRIVATE
    NEXIGEN_MCAP_COMMIT="42aea98d01dbbfe14c53f787378ee3d70ddb2e61")

if(NEXIGEN_ENABLE_COM)
    target_sources(nexigen_storage_bench PRIVATE publish_bench.cpp)
    target_link_libraries(nexigen_storage_bench PRIVATE nexigen_com)
    target_compile_definitions(nexigen_storage_bench PRIVATE NEXIGEN_BENCH_HAS_COM=1)
endif()

if(NEXIGEN_BUILD_TESTS)
    add_executable(nexigen_storage_bench_tests
        tests/bench_tests.cpp
        bench_types.cpp
        payload_blob.cpp
        metrics.cpp
        sink.cpp
        custom_writer.cpp
        video_writer.cpp)
    target_link_libraries(nexigen_storage_bench_tests PRIVATE
        nexigen_core
        nexigen_mcap_bench
        nexigen_mcap_bench_impl
        Threads::Threads)
    target_include_directories(nexigen_storage_bench_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(nexigen_storage_bench_tests PRIVATE
        NEXIGEN_MCAP_COMMIT="42aea98d01dbbfe14c53f787378ee3d70ddb2e61")
    add_test(NAME nexigen_storage_bench_tests COMMAND nexigen_storage_bench_tests)
    set_tests_properties(nexigen_storage_bench_tests PROPERTIES
        TIMEOUT 300 LABELS "storage-benchmark")
endif()

# Deliberately no install()/export() rule: this target is a measurement tool,
# not a distributed artifact.

# Production-writer companion: drives the shipped nexigen::recorder API for the
# published telemetry and FullHD 16 bpp 50 Hz workloads.
add_executable(nexigen_production_bench production_main.cpp)
target_link_libraries(nexigen_production_bench PRIVATE nexigen_recorder Threads::Threads)

