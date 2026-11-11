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

# The profiler's Tracy backend and the vendored Tracy client exist only with ENGINE_PROFILER_TRACY (cmake/BagiPlatformOptions.cmake).
# One list of definitions on exactly these two files, compiled without the PCH, so the PCH of BECore stays the same in every build
# and TRACY_ENABLE never reaches a header another file sees
list(FILTER CORE_MODULE_SOURCES EXCLUDE REGEX "/Profiler/Backends/TracyProfilerBackend\\.cpp$")
if(ENGINE_PROFILER_TRACY)
    set(BE_TRACY_DIR "${PROJECT_ROOT}/src/Modules/tracy/public")
    set(BE_TRACY_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Profiler/Backends/TracyProfilerBackend.cpp"
        "${BE_TRACY_DIR}/TracyClient.cpp"
    )
    set_source_files_properties(${BE_TRACY_SOURCES} PROPERTIES
        COMPILE_DEFINITIONS "TRACY_ENABLE;TRACY_ON_DEMAND;TRACY_MANUAL_LIFETIME;TRACY_DELAYED_INIT"
        INCLUDE_DIRECTORIES "${BE_TRACY_DIR}"
        SKIP_PRECOMPILE_HEADERS ON
    )
    # Vendored code: its warnings are not ours to fix
    set_source_files_properties("${BE_TRACY_DIR}/TracyClient.cpp" PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
endif()

# Создаем статическую библиотеку BECore
add_library(BECoreModule STATIC ${CORE_MODULE_SOURCES})

# Группировка файлов в IDE
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Core" FILES ${CORE_MODULE_SOURCES})

if(ENGINE_PROFILER_TRACY)
    target_sources(BECoreModule PRIVATE ${BE_TRACY_SOURCES})
    source_group("Core/Profiler/Tracy" FILES ${BE_TRACY_SOURCES})
endif()

# Устанавливаем свойства для группировки в IDE
set_target_properties(BECoreModule PROPERTIES FOLDER "Modules")

# Настройка предкомпилированных заголовков для модуля
# PRIVATE означает, что PCH будет использоваться только для этого модуля
# Другие модули могут переиспользовать его через REUSE_FROM
target_precompile_headers(BECoreModule PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)


# Макрос должен совпадать с целями, переиспользующими PCH (MyGame с ImGui Vulkan), иначе -Wclang-cl-pch
target_compile_definitions(BECoreModule PRIVATE IMGUI_VULKAN_AVAILABLE=1)
# Подключаем директории с заголовками
# PUBLIC означает, что эти директории будут доступны всем, кто линкуется с BECoreModule
# Добавляем ${PROJECT_ROOT}/src/Modules для поддержки включений через <BECore/...>
# Добавляем путь к SDL модулю для доступа к SDL3ApplicationFabric из ApplicationFabric
# Добавляем путь к Events модулю для доступа к EventsProviderManager из CoreManager
# Добавляем путь к TaskSystem модулю для доступа к TaskManager из CoreManager
get_filename_component(SDL_MODULE_DIR "${PROJECT_ROOT}/src/Modules/SDL/src" ABSOLUTE)
get_filename_component(EVENTS_MODULE_SRC_DIR "${PROJECT_ROOT}/src/Modules/Events/src" ABSOLUTE)
get_filename_component(TASKSYSTEM_MODULE_SRC_DIR "${PROJECT_ROOT}/src/Modules/TaskSystem/src" ABSOLUTE)
get_filename_component(VULKAN_MODULE_SRC_DIR "${PROJECT_ROOT}/src/Modules/Vulkan/src" ABSOLUTE)
target_include_directories(BECoreModule PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
    ${SDL_MODULE_DIR}
    ${EVENTS_MODULE_SRC_DIR}
    ${TASKSYSTEM_MODULE_SRC_DIR}
    ${VULKAN_MODULE_SRC_DIR}
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
    nlohmann_json
    stb
    SDL3-shared
)

# DbgHelp for stack trace capture (Windows only)
if(WIN32)
    target_link_libraries(BECoreModule PUBLIC dbghelp)
endif()

# =============================================================================
# Reflection Code Generation
# =============================================================================
# Dirs for BECoreModule are defined in CI/meta_generator/reflection_targets.json
if(COMMAND add_reflection_target)
    add_reflection_target(BECoreModule)
endif()
