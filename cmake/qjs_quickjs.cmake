# QuickJS engine: FetchContent + static target `quickjs`.
# Intended to be included once from this project's CMakeLists.txt (see include_guard).

include_guard(GLOBAL)

set(QJS_QUICKJS_REV "d7ae12ae71dfd6ab2997527d295014a8996fa0f9"
    CACHE STRING "Git revision (tag or commit) for bellard/quickjs FetchContent")

include(FetchContent)
FetchContent_Declare(quickjs_fc
    GIT_REPOSITORY https://github.com/bellard/quickjs.git
    GIT_TAG        ${QJS_QUICKJS_REV})
message(STATUS "qjs: FetchContent QuickJS ${QJS_QUICKJS_REV}")
FetchContent_MakeAvailable(quickjs_fc)

set(QJS_QUICKJS_DIR "${quickjs_fc_SOURCE_DIR}")
if(NOT EXISTS "${QJS_QUICKJS_DIR}/quickjs.c")
    message(FATAL_ERROR "QuickJS missing under ${QJS_QUICKJS_DIR}")
endif()

if(EXISTS "${QJS_QUICKJS_DIR}/VERSION")
    file(READ "${QJS_QUICKJS_DIR}/VERSION" _v)
    string(STRIP "${_v}" QJS_CONFIG_VERSION)
else()
    set(QJS_CONFIG_VERSION "unknown")
endif()

# Generated headers live under this subproject's binary dir (stable when used via add_subdirectory).
set(QJS_QUICKJS_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/qjs_quickjs_include")
file(MAKE_DIRECTORY "${QJS_QUICKJS_PUBLIC_INCLUDE_DIR}")
file(COPY "${QJS_QUICKJS_DIR}/quickjs.h" "${QJS_QUICKJS_DIR}/quickjs-libc.h"
    DESTINATION "${QJS_QUICKJS_PUBLIC_INCLUDE_DIR}")

add_library(quickjs STATIC
    "${QJS_QUICKJS_DIR}/quickjs.c"
    "${QJS_QUICKJS_DIR}/quickjs-libc.c"
    "${QJS_QUICKJS_DIR}/libregexp.c"
    "${QJS_QUICKJS_DIR}/libunicode.c"
    "${QJS_QUICKJS_DIR}/cutils.c"
    "${QJS_QUICKJS_DIR}/dtoa.c")

target_include_directories(quickjs PRIVATE "${QJS_QUICKJS_DIR}")
target_include_directories(quickjs PUBLIC "$<BUILD_INTERFACE:${QJS_QUICKJS_PUBLIC_INCLUDE_DIR}>")
target_compile_definitions(quickjs PRIVATE CONFIG_VERSION="${QJS_CONFIG_VERSION}")
target_compile_options(quickjs PRIVATE -w)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(quickjs PRIVATE NDEBUG)
    target_compile_options(quickjs PRIVATE -O3 -ffunction-sections -fdata-sections)
endif()

if(MSVC)
    message(FATAL_ERROR "MSVC is not supported; use MinGW-w64 or Clang on Windows.")
elseif(WIN32)
    target_compile_definitions(quickjs PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(UNIX)
    target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)
    target_link_libraries(quickjs PRIVATE m dl)
    if(NOT APPLE)
        target_link_libraries(quickjs PRIVATE pthread)
    endif()
endif()
