# CoreSDL Module CMake Configuration
# Этот модуль содержит весь код, связанный с SDL

# Получаем путь к корню проекта (на 2 уровня выше)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

# Определяем путь к Boost заголовкам
set(BOOST_HEADERS_DIR "${PROJECT_ROOT}/src/Modules/Core/Boost")

# SDL3 - зависимость модуля
include(FetchContent)
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG main
)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3)

# Поиск исходных файлов модуля в директории src
file(GLOB_RECURSE SDL_MODULE_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
)

# Сохраняем список исходных файлов в переменную для использования в родительском CMakeLists.txt
set(CORESDL_MODULE_SOURCES ${SDL_MODULE_SOURCES} PARENT_SCOPE)

# Сохраняем пути включения для использования в родительском CMakeLists.txt
set(CORESDL_MODULE_INCLUDE_DIRS
    ${PROJECT_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CoreSDL
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${BOOST_HEADERS_DIR}
    ${pugixml_SOURCE_DIR}/src
    PARENT_SCOPE
)

# Определяем правильный таргет SDL3
# Проверяем, какой таргет доступен после FetchContent_MakeAvailable
set(SDL3_TARGET "")
if(TARGET SDL3-shared)
    set(SDL3_TARGET SDL3-shared)
elseif(TARGET SDL3::SDL3-shared)
    set(SDL3_TARGET SDL3::SDL3-shared)
elseif(TARGET SDL3-static)
    set(SDL3_TARGET SDL3-static)
elseif(TARGET SDL3::SDL3-static)
    set(SDL3_TARGET SDL3::SDL3-static)
elseif(TARGET SDL3::SDL3)
    set(SDL3_TARGET SDL3::SDL3)
elseif(TARGET SDL3)
    set(SDL3_TARGET SDL3)
endif()

if(SDL3_TARGET STREQUAL "")
    message(FATAL_ERROR "SDL3 target not found. Available targets may need to be checked.")
endif()

# Сохраняем зависимости для использования в родительском CMakeLists.txt
set(CORESDL_MODULE_LIBRARIES
    CoreModule
    MathModule
    EventsModule
    ${SDL3_TARGET}
    pugixml
    PARENT_SCOPE
)

# Сохраняем имя таргета SDL3 для копирования DLL (если нужно)
set(CORESDL_SDL3_TARGET ${SDL3_TARGET} PARENT_SCOPE)

# Сохраняем путь к предкомпилированным заголовкам
set(CORESDL_MODULE_PCH ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h PARENT_SCOPE)


