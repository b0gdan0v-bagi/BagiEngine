# EA Libraries Module (Vendored EABase + EASTL)
# Provides EASTL static library with EABase headers

set(EA_EABASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/EABase")
set(EA_EASTL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/EASTL")

# Verify vendored files exist
if(NOT EXISTS "${EA_EABASE_DIR}/include/Common/EABase/eabase.h")
    message(FATAL_ERROR "EABase not found. Please copy EABase headers to: ${EA_EABASE_DIR}/include/Common/EABase/")
endif()
if(NOT EXISTS "${EA_EASTL_DIR}/include/EASTL/vector.h")
    message(FATAL_ERROR "EASTL not found. Please copy EASTL headers to: ${EA_EASTL_DIR}/include/EASTL/")
endif()

# Collect EASTL source files
file(GLOB EASTL_SOURCES "${EA_EASTL_DIR}/source/*.cpp")

# Create EASTL static library
add_library(EASTL STATIC ${EASTL_SOURCES})

target_include_directories(EASTL PUBLIC
    "${EA_EABASE_DIR}/include/Common"
    "${EA_EASTL_DIR}/include"
)

# EASTL requires at least C++17
target_compile_features(EASTL PUBLIC cxx_std_17)

# Disable warnings for external library
if(MSVC)
    target_compile_options(EASTL PRIVATE /W0)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(EASTL PRIVATE -w)
endif()

# Group in IDE
set_target_properties(EASTL PROPERTIES FOLDER "External")
