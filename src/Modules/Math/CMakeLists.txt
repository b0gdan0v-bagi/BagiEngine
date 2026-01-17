# Math Module CMake Configuration
# Статическая библиотека с математическими классами

# Получаем путь к корню проекта (на 3 уровня выше)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

# Поиск исходных файлов модуля в директории src
file(GLOB_RECURSE MATH_MODULE_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# Создаем статическую библиотеку Math
add_library(MathModule STATIC ${MATH_MODULE_SOURCES})

# Группировка файлов в IDE
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Math" FILES ${MATH_MODULE_SOURCES})

# Устанавливаем свойства для группировки в IDE
set_target_properties(MathModule PROPERTIES FOLDER "Modules")

# Настройка предкомпилированных заголовков для модуля
# REUSE_FROM позволяет переиспользовать PCH из BECoreModule без перекомпиляции
# Это критически ускоряет сборку, так как PCH компилируется только один раз
target_precompile_headers(MathModule REUSE_FROM BECoreModule)

# Для MSVC: автоматическое включение PCH через Force Include
# Это позволяет использовать PCH без явного #include в каждом файле
if(MSVC)
    target_compile_options(MathModule PRIVATE /FI"BECore/pch.h")
endif()

# Подключаем директории с заголовками
# Чтобы файлы из src/Modules/Math/src/Math/ были доступны через <Math/Color.h>,
# нужно добавить ${PROJECT_ROOT}/src/Modules/Math/src в include directories.
# Тогда путь Math/Color.h будет доступен как <Math/Color.h>.
add_library(MathModuleInterface INTERFACE)
target_include_directories(MathModuleInterface INTERFACE
    $<BUILD_INTERFACE:${PROJECT_ROOT}/src/Modules/Math/src>
    $<INSTALL_INTERFACE:include>
)

# Подключаем интерфейс к основной библиотеке
target_link_libraries(MathModule PUBLIC MathModuleInterface)

# Также добавляем стандартные пути включения для совместимости
target_include_directories(MathModule PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
)

# Подключаем зависимости
# MathModule зависит от BECoreModule (использует BECore::String)
target_link_libraries(MathModule PUBLIC
    BECoreModule
)
