# Core Module CMake Configuration
# Статическая библиотека с базовыми классами и интерфейсами

# Получаем путь к корню проекта (на 3 уровня выше)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

# Определяем путь к Boost заголовкам
set(BOOST_HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Boost")

# Проверяем наличие заголовков Boost
if(NOT EXISTS "${BOOST_HEADERS_DIR}/boost")
    message(WARNING "Boost headers not found in ${BOOST_HEADERS_DIR}/boost")
    message(STATUS "Run cmake/download_boost_headers.ps1 to download Boost headers")
    message(STATUS "Or run cmake/copy_boost_headers.ps1 -BoostSourcePath <path> to copy from existing installation")
endif()

message(STATUS "Using Boost headers from: ${BOOST_HEADERS_DIR}")

# Зависимости модуля Core
include(FetchContent)

# Устанавливаем минимальную версию политики для совместимости со старыми CMakeLists.txt в зависимостях (например, EABase, pugixml)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

# SDL3 - нужен для компиляции ImGui SDL3 бэкендов
# FetchContent_Declare здесь, но MakeAvailable будет вызван только если нужен для imgui
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG main
)

# ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
)

FetchContent_MakeAvailable(imgui)

# MagicEnum
FetchContent_Declare(
  magic_enum
  GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
  GIT_TAG v0.9.5
)

FetchContent_MakeAvailable(magic_enum)

# PugiXML
FetchContent_Declare(
  pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG v1.14
)

set(EA_KITS_VERBOSE OFF CACHE BOOL "" FORCE)
set(EASTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EASTL_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(pugixml)

# EABase - требуется для EASTL
FetchContent_Declare(
  eabase
  GIT_REPOSITORY https://github.com/electronicarts/EABase.git
  GIT_TAG master
  GIT_SUBMODULES ""  # Опустошаем список субмодулей, чтобы он не лез в тесты
)

FetchContent_MakeAvailable(eabase)

# EASTL
FetchContent_Declare(
  eastl
  GIT_REPOSITORY https://github.com/electronicarts/EASTL.git
  GIT_TAG master
  GIT_SUBMODULES ""
)

FetchContent_MakeAvailable(eastl)

# fmt
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 11.0.2
)

FetchContent_MakeAvailable(fmt)

# EnTT - needed for EventBase used in Assert system
# Note: This is the same declaration as in EventsModule, CMake will reuse it
FetchContent_Declare(
  entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v3.13.2
)

FetchContent_MakeAvailable(entt)

# Создаем статическую библиотеку ImGui
FetchContent_GetProperties(imgui
    SOURCE_DIR IMGUI_SOURCE_DIR
    POPULATED IMGUI_POPULATED
)

if(IMGUI_POPULATED AND IMGUI_SOURCE_DIR AND EXISTS "${IMGUI_SOURCE_DIR}")
    set(IMGUI_SOURCES
        ${IMGUI_SOURCE_DIR}/imgui.cpp
        ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
        ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
        ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
        ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
    )
    
    # Проверяем наличие бэкендов для SDL3
    set(HAS_SDL3_BACKEND OFF)
    if(EXISTS "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp")
        list(APPEND IMGUI_SOURCES ${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp)
        set(HAS_SDL3_BACKEND ON)
    endif()
    
    if(EXISTS "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
        list(APPEND IMGUI_SOURCES ${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
        set(HAS_SDL3_BACKEND ON)
    endif()
    
    # Если есть SDL3 бэкенды, нужно получить доступ к SDL3 заголовкам
    if(HAS_SDL3_BACKEND)
        # Проверяем, был ли SDL3 уже получен (например, в SDL модуле)
        FetchContent_GetProperties(SDL3
            SOURCE_DIR SDL3_SOURCE_DIR
            POPULATED SDL3_POPULATED
        )
        
        # Если SDL3 еще не получен, получаем его здесь
        if(NOT SDL3_POPULATED)
            set(SDL_SHARED ON CACHE BOOL "" FORCE)
            set(SDL_STATIC OFF CACHE BOOL "" FORCE)
            set(SDL_TESTS OFF CACHE BOOL "" FORCE)
            FetchContent_MakeAvailable(SDL3)
        endif()
        
        # Получаем свойства SDL3 после MakeAvailable
        FetchContent_GetProperties(SDL3
            SOURCE_DIR SDL3_SOURCE_DIR
            POPULATED SDL3_POPULATED
        )
    endif()
    
    add_library(imgui STATIC ${IMGUI_SOURCES})
    
    target_include_directories(imgui PUBLIC
        ${IMGUI_SOURCE_DIR}
        ${IMGUI_SOURCE_DIR}/backends
    )
    
    # Если есть SDL3 бэкенды, подключаем SDL3 к imgui
    if(HAS_SDL3_BACKEND AND SDL3_POPULATED)
        # Проверяем наличие статической или динамической библиотеки SDL3
        if(TARGET SDL3-static)
            target_link_libraries(imgui PUBLIC SDL3-static)
        elseif(TARGET SDL3::SDL3-static)
            target_link_libraries(imgui PUBLIC SDL3::SDL3-static)
        elseif(TARGET SDL3-shared)
            target_link_libraries(imgui PUBLIC SDL3-shared)
        elseif(TARGET SDL3::SDL3-shared)
            target_link_libraries(imgui PUBLIC SDL3::SDL3-shared)
        endif()
    endif()
    
    set_target_properties(imgui PROPERTIES FOLDER "Modules/Dependencies")
    
    # Группировка файлов ImGui в Visual Studio
    source_group("ImGui" FILES ${IMGUI_SOURCES})
    
    message(STATUS "ImGui sources directory: ${IMGUI_SOURCE_DIR}")
endif()

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
get_filename_component(SDL_MODULE_DIR "${PROJECT_ROOT}/src/Modules/SDL/src" ABSOLUTE)
get_filename_component(EVENTS_MODULE_SRC_DIR "${PROJECT_ROOT}/src/Modules/Events/src" ABSOLUTE)
target_include_directories(BECoreModule PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
    ${SDL_MODULE_DIR}
    ${EVENTS_MODULE_SRC_DIR}
    ${BOOST_HEADERS_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${pugixml_SOURCE_DIR}/src
    ${magic_enum_SOURCE_DIR}/include
    ${eabase_SOURCE_DIR}/include
    ${eastl_SOURCE_DIR}/include
    ${fmt_SOURCE_DIR}/include
    ${entt_SOURCE_DIR}/src
)

# Подключаем зависимости
# BECoreModule зависит от MathModule через XmlNode (использует Math::Color)
# Но MathModule должен быть подключен в главном CMakeLists.txt перед BECoreModule
# Примечание: BECoreModule использует EventsProviderManager, но не линкуется с EventsModule
# чтобы избежать циклической зависимости (EventsModule линкуется с BECoreModule).
# Include директории Events добавлены напрямую выше.
target_link_libraries(BECoreModule PUBLIC
    MathModule
    pugixml
    magic_enum::magic_enum
    imgui
	EASTL   
    EABase
    fmt::fmt
)
