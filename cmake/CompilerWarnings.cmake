function(nexigen_set_warnings target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Werror                 # Все предупреждения = ошибки компиляции
            -Wshadow                # Запрет перекрытия имен переменных
            -Wnon-virtual-dtor      # Предупреждение о невиртуальных деструкторах
            -Wold-style-cast        # Запрет сишных кастов (type)val
            -Wcast-align            # Проверка выравнивания указателей (важно для Real-Time)
            -Wunused                # Запрет неиспользуемых переменных/функций
            -Woverloaded-virtual    # Защита от скрытия виртуальных методов
            -Wconversion            # Запрет неявных сужающих преобразований (int -> short)
            -Wsign-conversion       # Запрет неявного смешивания signed и unsigned
            -Wnull-dereference      # Отлов разыменования nullptr на этапе компиляции
            -Wdouble-promotion      # Защита от случайной конвертации float в double
            -Wformat=2              # Жесткая проверка строк форматирования
        )
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(${target_name} INTERFACE
            -Wduplicated-cond       # Одинаковые условия в if/else
            -Wduplicated-branches   # Одинаковый код в разных ветках
            -Wlogical-op            # Подозрительные логические операции
            -Wuseless-cast          # Каст типа в самого себя
        )
    endif()
endfunction()
