cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(Tests CXX)

Include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.9.1
)

message(STATUS "TESTS: Fetching Catch2...")
FetchContent_MakeAvailable(Catch2)

set(TESTS_SOURCES
    "filesystem.cpp"
)

add_executable(tests ${TESTS_SOURCES})
target_include_directories(tests PRIVATE ../src)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)
target_link_libraries(tests PRIVATE engine)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
enable_testing()
catch_discover_tests(tests TEST_SPEC "[engine]")

