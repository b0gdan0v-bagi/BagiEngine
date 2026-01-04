# Events Module CMake Configuration
# Статическая библиотека с системой событий

# Получаем путь к корню проекта (на 3 уровня выше)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

# EnTT - зависимость модуля Events
include(FetchContent)
FetchContent_Declare(
  entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v3.13.2
)

FetchContent_MakeAvailable(entt)

# Поиск исходных файлов модуля в директории src
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
target_precompile_headers(EventsModule PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h)

# Подключаем директории с заголовками
# Добавляем ${CMAKE_CURRENT_SOURCE_DIR}/src для поддержки включений через <Events/...>
target_include_directories(EventsModule PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${entt_SOURCE_DIR}/src
)

# Подключаем зависимости
# EventsModule зависит от CoreModule (RefCounted, IntrusivePtr, PassKey) и MathModule (RenderEvents)
# entt - header-only библиотека, достаточно include директорий (уже добавлены выше)
target_link_libraries(EventsModule PUBLIC
    CoreModule
    MathModule
)

