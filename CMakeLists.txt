cmake_minimum_required(VERSION 3.25)

project(NoxMcp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_VS_GLOBALS "UseMultiToolTask=true" "EnforceProcessCountAcrossBuilds=true")
endif()
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/MP>)
endif()

option(NOXMCP_BUILD_TESTS "Build standalone NoxMcp test executable (NoxMcpTest)" ON)

# -----------------------------------------------------------------------------
# Third-Party Dependencies (cpp-httplib + nlohmann/json header-only)
# -----------------------------------------------------------------------------
set(NOXMCP_HTTPLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendors/cpp-httplib" CACHE PATH "Path to cpp-httplib directory")
if(NOT EXISTS "${NOXMCP_HTTPLIB_DIR}/httplib.h")
    message(FATAL_ERROR "cpp-httplib not found in '${NOXMCP_HTTPLIB_DIR}'. Run scripts/install_dependencies.bat first.")
endif()

set(NOXMCP_JSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendors/nlohmann" CACHE PATH "Path to nlohmann/json.hpp directory")
if(NOT EXISTS "${NOXMCP_JSON_DIR}/json.hpp")
    message(FATAL_ERROR "nlohmann/json.hpp not found in '${NOXMCP_JSON_DIR}'. Run scripts/install_dependencies.bat first.")
endif()

# -----------------------------------------------------------------------------
# Core NoxMcp Library
# -----------------------------------------------------------------------------
file(GLOB_RECURSE NOXMCP_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/NoxMcp/*.h"
)
file(GLOB_RECURSE NOXMCP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_library(NoxMcp STATIC ${NOXMCP_HEADERS} ${NOXMCP_SOURCES})
add_library(NoxMcp::Core ALIAS NoxMcp)
add_library(NoxMcp::NoxMcp ALIAS NoxMcp)

target_include_directories(NoxMcp
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${NOXMCP_JSON_DIR}"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${NOXMCP_HTTPLIB_DIR}"
)
target_compile_features(NoxMcp PUBLIC cxx_std_20)
target_compile_definitions(NoxMcp
    PRIVATE
        $<$<BOOL:${WIN32}>:NOMINMAX>
        $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
        _CRT_SECURE_NO_WARNINGS
)

if(WIN32)
    target_link_libraries(NoxMcp PUBLIC ws2_32 bcrypt)
endif()

# -----------------------------------------------------------------------------
# Standalone Tests (tests/NoxMcpTest)
# -----------------------------------------------------------------------------
if(NOXMCP_BUILD_TESTS)
    add_executable(NoxMcpTest tests/NoxMcpTest/main.cpp)
    target_include_directories(NoxMcpTest PRIVATE "${NOXMCP_HTTPLIB_DIR}")
    target_compile_definitions(NoxMcpTest PRIVATE
        $<$<BOOL:${WIN32}>:NOMINMAX>
        $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
        _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries(NoxMcpTest PRIVATE NoxMcp::Core)
endif()
