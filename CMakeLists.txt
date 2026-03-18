cmake_minimum_required(VERSION 3.16)

project(slowjs LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SLOWJS_BUILD_SHARED "Build slowjs as shared library" OFF)

set(BUILD_SHARED_LIBS ${SLOWJS_BUILD_SHARED})

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

include(cmake/quickjs.cmake)

# ---------------------------------------------------------------------------
# slowjs library
# ---------------------------------------------------------------------------

add_library(slowjs STATIC
    src/JSEngine.cpp
)

target_include_directories(slowjs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(slowjs PUBLIC
    quickjs
)

