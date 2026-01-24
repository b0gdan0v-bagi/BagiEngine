# TaskSystem Module CMake Configuration
# Статическая библиотека с системой задач на C++20 корутинах

# Получаем путь к корню проекта (на 3 уровня выше)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

# Поиск исходных файлов модуля в директории src (рекурсивно, включая подпапки)
file(GLOB_RECURSE TASKSYSTEM_MODULE_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# Создаем статическую библиотеку TaskSystem
add_library(TaskSystemModule STATIC ${TASKSYSTEM_MODULE_SOURCES})

# Группировка файлов в IDE
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "TaskSystem" FILES ${TASKSYSTEM_MODULE_SOURCES})

# Устанавливаем свойства для группировки в IDE
set_target_properties(TaskSystemModule PROPERTIES FOLDER "Modules")

# Настройка предкомпилированных заголовков для модуля
# REUSE_FROM позволяет переиспользовать PCH из BECoreModule без перекомпиляции
# Это критически ускоряет сборку, так как PCH компилируется только один раз
target_precompile_headers(TaskSystemModule REUSE_FROM BECoreModule)

# Автоматическое включение PCH через Force Include
# Это позволяет использовать PCH без явного #include в каждом файле
if(MSVC)
    # MSVC: Force Include через /FI
    target_compile_options(TaskSystemModule PRIVATE /FI"BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    # Clang/AppleClang: Force Include через -include
    target_compile_options(TaskSystemModule PRIVATE -include "BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC: Force Include через -include
    target_compile_options(TaskSystemModule PRIVATE -include "BECore/pch.h")
endif()

# Подключаем директории с заголовками
# Чтобы файлы из src/Modules/TaskSystem/src/TaskSystem/ были доступны через <TaskSystem/TaskManager.h>
add_library(TaskSystemModuleInterface INTERFACE)
target_include_directories(TaskSystemModuleInterface INTERFACE
    $<BUILD_INTERFACE:${PROJECT_ROOT}/src/Modules/TaskSystem/src>
    $<INSTALL_INTERFACE:include>
)

# Подключаем интерфейс к основной библиотеке
target_link_libraries(TaskSystemModule PUBLIC TaskSystemModuleInterface)

# Также добавляем стандартные пути включения для совместимости
target_include_directories(TaskSystemModule PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
)

# Подключаем зависимости
# TaskSystemModule зависит от BECoreModule (Singleton, PassKey, Logger, Assert) и EventsModule (для awaitable событий)
target_link_libraries(TaskSystemModule PUBLIC
    BECoreModule
    EventsModule
)
