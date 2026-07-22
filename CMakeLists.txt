cmake_minimum_required(VERSION 3.16)
project(OrderBookEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization flags for Release build
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
endif()

# Main engine library
add_library(engine_lib
    src/matching_engine.cpp
)
target_include_directories(engine_lib PUBLIC include)

# Benchmark binary
add_executable(benchmark src/benchmark.cpp)
target_link_libraries(benchmark engine_lib)

# Google Test setup
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Test binary
add_executable(engine_tests tests/engine_tests.cpp)
target_link_libraries(engine_tests engine_lib GTest::gtest_main)

# Enable testing
enable_testing()
include(GoogleTest)
gtest_discover_tests(engine_tests)
