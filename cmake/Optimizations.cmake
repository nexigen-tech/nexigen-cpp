include(CheckIPOSupported)

function(nexigen_set_optimizations target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Базовые ограничения архитектуры Real-Time (действуют всегда: и в Debug, и в Release)
        target_compile_options(${target_name} INTERFACE
            -fno-exceptions         # Полный запрет try/catch/throw
            -fno-rtti               # Полный запрет dynamic_cast и typeid
        )

        # Экстремальные оптимизации только для Release и RelWithDebInfo
        target_compile_options(${target_name} INTERFACE
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:
                -O3
                -fno-signed-zeros   # Ускорение математических операций с плавающей точкой
                -fno-trapping-math
                -fomit-frame-pointer
            >
        )

        # Host-specific tuning is useful for dedicated HIL hardware but makes
        # binaries non-portable. NEXIGEN_NATIVE_ARCH=OFF keeps the Release
        # pipeline above and omits only these two flags.
        if(NEXIGEN_NATIVE_ARCH)
            target_compile_options(${target_name} INTERFACE
                $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:
                    -march=native
                    -mtune=native
                >
            )
        endif()
    endif()

    # Включаем Link-Time Optimization (LTO / IPO) для Release-сборок.
    # Analysis #15: INTERPROCEDURAL_OPTIMIZATION, поставленный на INTERFACE
    # цель, ничего не собирает — no-op при честном "🚀 enabled". The
    # canonical variable applies compile+link LTO to every target created
    # AFTER this point (nexigen_set_optimizations runs before target
    # definitions) and honours the Debug/Release split.
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
        if(lto_supported)
            # The variable must outlive this function scope (function-local
            # set dies with the call; targets are created after, at TOP
            # scope): PARENT_SCOPE publishes it precisely where targets
            # read it at generation time.
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE PARENT_SCOPE)
            message(STATUS "🚀 LTO (Link-Time Optimization) enabled")
        else()
            message(WARNING "LTO is not supported: ${lto_error}")
        endif()
    endif()
endfunction()
