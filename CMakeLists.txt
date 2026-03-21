# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(unistreak_cli VERSION 1.0.0 LANGUAGES C)

# Дата сборки в формате dd.mm.yy
string(TIMESTAMP BUILD_DATE "%d.%m.%Y")

# Стандарт C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Опции сборки
option(ENABLE_STATIC "Build static executable" ON)
option(ENABLE_DEBUG "Build with debug symbols" OFF)

# Исходные файлы
set(SOURCES
    main.c
    unistreak.c
    cJSON.c
)

# Заголовочные файлы
set(HEADERS
    unistreak.h
    cJSON.h
)

# Создаём исполняемый файл
add_executable(unistreak_cli ${SOURCES} ${HEADERS})

# Определение макроса версии
target_compile_definitions(unistreak_cli PRIVATE
    UNISTREAK_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    UNISTREAK_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    UNISTREAK_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    BUILD_DATE="${BUILD_DATE}"
)

# Платформенно-зависимые настройки
if(WIN32)
    # Windows: линкуем с ws2_32 для сокетов
    target_link_libraries(unistreak_cli PRIVATE ws2_32)
    
    # Статическая линковка для портативности
    if(ENABLE_STATIC)
        set_target_properties(unistreak_cli PROPERTIES
            LINK_FLAGS "-static -static-libgcc"
        )
    endif()
    
    # Имя файла с архитектурой
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set_target_properties(unistreak_cli PROPERTIES OUTPUT_NAME "unistreak_cli-windows-x64")
    else()
        set_target_properties(unistreak_cli PROPERTIES OUTPUT_NAME "unistreak_cli-win32-x86")
    endif()
else()
    # Linux: линкуем с pthread и math
    target_link_libraries(unistreak_cli PRIVATE pthread m)
    
    # Статическая линковка (опционально)
    if(ENABLE_STATIC)
        set_target_properties(unistreak_cli PROPERTIES
            LINK_FLAGS "-static"
        )
    endif()
    
    # Имя файла с архитектурой
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set_target_properties(unistreak_cli PROPERTIES OUTPUT_NAME "unistreak_cli-linux-x86_64")
    else()
        set_target_properties(unistreak_cli PROPERTIES OUTPUT_NAME "unistreak_cli-linux-x86")
    endif()
endif()

# Флаги компиляции для всех платформ
target_compile_options(unistreak_cli PRIVATE
    $<$<C_COMPILER_ID:Clang,GNU>:
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wstrict-prototypes
    >
    $<$<C_COMPILER_ID:MSVC>:
        /W4
        /permissive-
    >
)

# Отладочные флаги
if(ENABLE_DEBUG)
    target_compile_options(unistreak_cli PRIVATE
        $<$<C_COMPILER_ID:Clang,GNU>: -g -O0>
        $<$<C_COMPILER_ID:MSVC>: /Zi /Od>
    )
else()
    target_compile_options(unistreak_cli PRIVATE
        $<$<C_COMPILER_ID:Clang,GNU>: -O2>
        $<$<C_COMPILER_ID:MSVC>: /O2>
    )
endif()

# Установка (опционально)
install(TARGETS unistreak_cli
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
