# pugixml - Lightweight XML parser (Vendored)
# Light-weight, simple and fast XML parser for C++ with XPath support

add_library(pugixml STATIC pugixml.cpp)

target_include_directories(pugixml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# pugixml requires at least C++11
target_compile_features(pugixml PUBLIC cxx_std_11)

# Disable warnings for external library
if(MSVC)
    target_compile_options(pugixml PRIVATE /W0)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(pugixml PRIVATE -w)
endif()

# Group in IDE
set_target_properties(pugixml PROPERTIES FOLDER "External")
