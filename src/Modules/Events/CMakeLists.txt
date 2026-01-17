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
    ${entt_SOURCE_DIR}/src
)

# Подключаем зависимости
# EventsModule зависит от BECoreModule (RefCounted, IntrusivePtr, PassKey) и MathModule (RenderEvents)
# entt - header-only библиотека, достаточно include директорий (уже добавлены выше)
target_link_libraries(EventsModule PUBLIC
    BECoreModule
    MathModule
)

