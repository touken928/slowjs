# Unit test executable `qjs_tests` (optional). Requires QJS_BUILD_TESTS=ON.

include_guard(GLOBAL)

if(NOT TARGET GTest::gtest_main)
    include(FetchContent)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0)
    FetchContent_MakeAvailable(googletest)
endif()

enable_testing()
add_executable(qjs_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/js_engine_test.cc")
target_link_libraries(qjs_tests PRIVATE GTest::gtest_main qjs::qjs)

include(GoogleTest)
gtest_discover_tests(qjs_tests)
