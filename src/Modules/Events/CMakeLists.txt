# Events Module CMake Configuration
# Статическая библиотека с системой событий

# Получаем путь к корню проекта (на 3 уровня выше)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

# Поиск исходных файлов модуля в директории src (рекурсивно, включая подпапки)
file(GLOB_RECURSE EVENTS_MODULE_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# Создаем статическую библиотеку Events
add_library(EventsModule STATIC ${EVENTS_MODULE_SOURCES})

# Группировка файлов в IDE
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Events" FILES ${EVENTS_MODULE_SOURCES})

# Устанавливаем свойства для группировки в IDE
set_target_properties(EventsModule PROPERTIES FOLDER "Modules")

# Настройка предкомпилированных заголовков для модуля
# REUSE_FROM позволяет переиспользовать PCH из BECoreModule без перекомпиляции
# Это критически ускоряет сборку, так как PCH компилируется только один раз
target_precompile_headers(EventsModule REUSE_FROM BECoreModule)

# Автоматическое включение PCH через Force Include
# Это позволяет использовать PCH без явного #include в каждом файле
if(MSVC)
    # MSVC: Force Include через /FI
    target_compile_options(EventsModule PRIVATE /FI"BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Clang: Force Include через -include
    target_compile_options(EventsModule PRIVATE -include "BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC: Force Include через -include
    target_compile_options(EventsModule PRIVATE -include "BECore/pch.h")
endif()

# Подключаем директории с заголовками
# Чтобы файлы из src/Modules/Events/src/Events/ были доступны через <Events/EventsProviderManager.h>,
# нужно добавить ${PROJECT_ROOT}/src/Modules/Events/src в include directories.
# Тогда путь Events/EventsProviderManager.h будет доступен как <Events/EventsProviderManager.h>.
add_library(EventsModuleInterface INTERFACE)
target_include_directories(EventsModuleInterface INTERFACE
    $<BUILD_INTERFACE:${PROJECT_ROOT}/src/Modules/Events/src>
    $<INSTALL_INTERFACE:include>
)

# Подключаем интерфейс к основной библиотеке
target_link_libraries(EventsModule PUBLIC EventsModuleInterface)

# Также добавляем стандартные пути включения для совместимости
target_include_directories(EventsModule PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
)

# Подключаем зависимости
# EventsModule зависит от BECoreModule (RefCounted, IntrusivePtr, PassKey) и MathModule (RenderEvents)
# EnTT - header-only библиотека
target_link_libraries(EventsModule PUBLIC
    BECoreModule
    MathModule
    EnTT
)

