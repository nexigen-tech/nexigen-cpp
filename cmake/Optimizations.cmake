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
                -march=native       # Оптимизация под инструкции конкретного CPU машины
                -mtune=native
                -fno-signed-zeros   # Ускорение математических операций с плавающей точкой
                -fno-trapping-math
                -fomit-frame-pointer
            >
        )
    endif()

    # Включаем Link-Time Optimization (LTO / IPO) для Release-сборок
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
        if(lto_supported)
            message(STATUS "🚀 LTO (Link-Time Optimization) enabled")
            set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(WARNING "LTO is not supported: ${lto_error}")
        endif()
    endif()
endfunction()
