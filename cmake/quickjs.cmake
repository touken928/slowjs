# QuickJS build config for slowjs (expects git submodule at third_party/quickjs)

set(QUICKJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quickjs)

if(NOT EXISTS ${QUICKJS_DIR}/quickjs.c)
    message(FATAL_ERROR "QuickJS not found. Please run: git submodule update --init --recursive")
endif()

set(QUICKJS_SOURCES
    ${QUICKJS_DIR}/quickjs.c
    ${QUICKJS_DIR}/quickjs-libc.c
    ${QUICKJS_DIR}/libregexp.c
    ${QUICKJS_DIR}/libunicode.c
    ${QUICKJS_DIR}/cutils.c
    ${QUICKJS_DIR}/dtoa.c
)

# Provide public include dir in build tree (copy headers; avoids Windows symlink perms)
set(QUICKJS_INCLUDE_DIR ${CMAKE_BINARY_DIR}/quickjs_include)
file(MAKE_DIRECTORY ${QUICKJS_INCLUDE_DIR})

set(QUICKJS_HEADERS
    quickjs.h
    quickjs-libc.h
)

foreach(header ${QUICKJS_HEADERS})
    if(EXISTS ${QUICKJS_DIR}/${header})
        file(COPY ${QUICKJS_DIR}/${header} DESTINATION ${QUICKJS_INCLUDE_DIR})
    endif()
endforeach()

add_library(quickjs STATIC ${QUICKJS_SOURCES})

target_include_directories(quickjs PRIVATE ${QUICKJS_DIR})
target_include_directories(quickjs PUBLIC ${QUICKJS_INCLUDE_DIR})

target_compile_definitions(quickjs PRIVATE CONFIG_VERSION="2024-01-13")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(quickjs PRIVATE NDEBUG)
    target_compile_options(quickjs PRIVATE -O3 -ffunction-sections -fdata-sections)
endif()

if(WIN32)
    if(MSVC)
        message(FATAL_ERROR "MSVC is not supported. Please use MinGW-w64 or Clang on Windows.")
    endif()
    target_compile_definitions(quickjs PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(UNIX)
    target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)
    target_link_libraries(quickjs PRIVATE m dl)
    if(NOT APPLE)
        target_link_libraries(quickjs PRIVATE pthread)
    endif()
endif()

if(MSVC)
    target_compile_options(quickjs PRIVATE /W3 /wd4244 /wd4267 /wd4996)
else()
    target_compile_options(quickjs PRIVATE -w)
endif()

