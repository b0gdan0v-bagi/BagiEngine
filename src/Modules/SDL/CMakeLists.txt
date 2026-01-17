# CoreSDL Module CMake Configuration
# Этот модуль содержит весь код, связанный с SDL

# Получаем путь к корню проекта (на 2 уровня выше)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

# Определяем путь к Boost заголовкам
set(BOOST_HEADERS_DIR "${PROJECT_ROOT}/src/Modules/BECore/Boost")

# SDL3 - зависимость модуля
include(FetchContent)

# Восстанавливаем состояние FetchContent для существующих исходников
restore_fetchcontent_if_exists(SDL3)

FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG main
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
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

# Создаем статическую библиотеку SDL
add_library(SDLModule STATIC ${SDL_MODULE_SOURCES})

# Группировка файлов в IDE
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "SDL" FILES ${SDL_MODULE_SOURCES})

# Устанавливаем свойства для группировки в IDE
set_target_properties(SDLModule PROPERTIES FOLDER "Modules")

# Настройка предкомпилированных заголовков для модуля
# REUSE_FROM позволяет переиспользовать PCH из BECoreModule без перекомпиляции
# Это критически ускоряет сборку, так как PCH компилируется только один раз
target_precompile_headers(SDLModule REUSE_FROM BECoreModule)

# Автоматическое включение PCH через Force Include
# Это позволяет использовать PCH без явного #include в каждом файле
if(MSVC)
    # MSVC: Force Include через /FI
    target_compile_options(SDLModule PRIVATE /FI"BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Clang: Force Include через -include
    target_compile_options(SDLModule PRIVATE -include "BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC: Force Include через -include
    target_compile_options(SDLModule PRIVATE -include "BECore/pch.h")
endif()

# Подключаем директории с заголовками
target_include_directories(SDLModule PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CoreSDL
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${BOOST_HEADERS_DIR}
    ${pugixml_SOURCE_DIR}/src
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

# Подключаем зависимости модуля
target_link_libraries(SDLModule PUBLIC
    BECoreModule
    MathModule
    EventsModule
    ${SDL3_TARGET}
    pugixml
)

# Сохраняем имя таргета SDL3 для копирования DLL (если нужно)
set(CORESDL_SDL3_TARGET ${SDL3_TARGET} PARENT_SCOPE)


