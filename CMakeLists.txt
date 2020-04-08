cmake_minimum_required(VERSION 3.13)

project(shq_test)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

# Tester executable

add_executable(tester test/test.cpp)

target_include_directories(tester PUBLIC ${PROJECT_SOURCE_DIR})

target_compile_options(tester PUBLIC
    -std=c++11
    -Wall
    -Wextra
    -Wpedantic
    -Wunreachable-code)

target_link_libraries(tester PUBLIC pthread rt)

add_test(NAME Tester COMMAND tester)

# Test deleter executable

add_executable(test_deleter test/deleter.cpp)

target_include_directories(test_deleter PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(test_deleter PUBLIC pthread rt)
        
target_compile_options(test_deleter PUBLIC
    -std=c++11
    -Wall
    -Wextra
    -Wpedantic
    -Wunreachable-code)
