cmake_minimum_required(VERSION 3.16)

project(qjs LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/quickjs.cmake)

add_library(qjs STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/js_engine.cc
)

target_include_directories(qjs PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(qjs PUBLIC quickjs)

add_library(qjs::qjs ALIAS qjs)
add_library(qjs::quickjs ALIAS quickjs)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(QJS_BUILD_TESTS "Build qjs unit tests (GoogleTest)" ON)
else()
    option(QJS_BUILD_TESTS "Build qjs unit tests (GoogleTest)" OFF)
endif()
if(QJS_BUILD_TESTS)
    include(cmake/googletest.cmake)
endif()
