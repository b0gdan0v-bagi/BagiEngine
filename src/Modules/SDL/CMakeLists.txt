# CoreSDL Module CMake Configuration
# Этот модуль содержит весь код, связанный с SDL

# Получаем путь к корню проекта (на 2 уровня выше)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

# Определяем путь к Boost заголовкам
set(BOOST_HEADERS_DIR "${PROJECT_ROOT}/src/Modules/BECore/Boost")

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
)

# Подключаем зависимости модуля
target_link_libraries(SDLModule PUBLIC
    BECoreModule
    EventsModule
    SDL3-shared
    pugixml
)

# Сохраняем имя таргета SDL3 для копирования DLL (если нужно)
set(CORESDL_SDL3_TARGET SDL3-shared PARENT_SCOPE)


