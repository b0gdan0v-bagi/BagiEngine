# Core Module CMake Configuration
# Статическая библиотека с базовыми классами и интерфейсами

# Получаем путь к корню проекта (на 3 уровня выше)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

# Поиск исходных файлов модуля
file(GLOB_RECURSE CORE_MODULE_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Создаем статическую библиотеку BECore
add_library(BECoreModule STATIC ${CORE_MODULE_SOURCES})

# Группировка файлов в IDE
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Core" FILES ${CORE_MODULE_SOURCES})

# Устанавливаем свойства для группировки в IDE
set_target_properties(BECoreModule PROPERTIES FOLDER "Modules")

# Настройка предкомпилированных заголовков для модуля
# PRIVATE означает, что PCH будет использоваться только для этого модуля
# Другие модули могут переиспользовать его через REUSE_FROM
target_precompile_headers(BECoreModule PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)

# Подключаем директории с заголовками
# PUBLIC означает, что эти директории будут доступны всем, кто линкуется с BECoreModule
# Добавляем ${PROJECT_ROOT}/src/Modules для поддержки включений через <BECore/...>
# Добавляем путь к SDL модулю для доступа к ApplicationSDLFabric из ApplicationFabric
# Добавляем путь к Events модулю для доступа к EventsProviderManager из CoreManager
# Добавляем путь к TaskSystem модулю для доступа к TaskManager из CoreManager
get_filename_component(SDL_MODULE_DIR "${PROJECT_ROOT}/src/Modules/SDL/src" ABSOLUTE)
get_filename_component(EVENTS_MODULE_SRC_DIR "${PROJECT_ROOT}/src/Modules/Events/src" ABSOLUTE)
get_filename_component(TASKSYSTEM_MODULE_SRC_DIR "${PROJECT_ROOT}/src/Modules/TaskSystem/src" ABSOLUTE)
target_include_directories(BECoreModule PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
    ${SDL_MODULE_DIR}
    ${EVENTS_MODULE_SRC_DIR}
    ${TASKSYSTEM_MODULE_SRC_DIR}
)

# Подключаем зависимости из SDK
# Примечание: BECoreModule использует EventsProviderManager, но не линкуется с EventsModule
# чтобы избежать циклической зависимости (EventsModule линкуется с BECoreModule).
# Include директории Events добавлены напрямую выше.
target_link_libraries(BECoreModule PUBLIC
    pugixml
    imgui
    EASTL
    fmt
    EnTT
    SDL3-shared
)

# =============================================================================
# Reflection Code Generation
# =============================================================================
# Generate reflection metadata for all BE_CLASS() files in specified directories
# DIRS automatically scans for headers containing BE_CLASS() macro
# SCAN_DIRS specifies additional directories to scan for derived classes (for factory generation)
if(COMMAND add_reflection_target)
    add_reflection_target(BECoreModule
        DIRS
            ${CMAKE_CURRENT_SOURCE_DIR}/Logger
            ${CMAKE_CURRENT_SOURCE_DIR}/Tests
            ${CMAKE_CURRENT_SOURCE_DIR}/Widgets
            ${CMAKE_CURRENT_SOURCE_DIR}/Resource
            ${CMAKE_CURRENT_SOURCE_DIR}/Math
        SCAN_DIRS
            ${CMAKE_CURRENT_SOURCE_DIR}/Logger
            ${CMAKE_CURRENT_SOURCE_DIR}/Tests
            ${CMAKE_CURRENT_SOURCE_DIR}/Widgets
            ${CMAKE_CURRENT_SOURCE_DIR}/Resource
            ${CMAKE_CURRENT_SOURCE_DIR}/Math
            ${PROJECT_ROOT}/src/Widgets
        INCLUDE_DIRS
            ${PROJECT_ROOT}/src/Modules
            ${PROJECT_ROOT}/src
    )
endif()
