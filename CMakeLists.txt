cmake_minimum_required(VERSION 3.13)

project(shq)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMPILE_FLAGS
    -std=c++11
    -Wall
    -Wextra
    -Wpedantic
    -Wunreachable-code)

# Tester executable

add_executable(tester test/test.cpp)

target_include_directories(tester PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_options(tester PUBLIC ${COMPILE_FLAGS})
target_link_libraries(tester PUBLIC pthread rt)

# Test deleter executable

add_executable(test_deleter test/deleter.cpp)

target_include_directories(test_deleter PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_options(test_deleter PUBLIC ${COMPILE_FLAGS})
target_link_libraries(test_deleter PUBLIC pthread rt)

# Example writer

add_executable(example_writer example/writer.cpp)

target_include_directories(example_writer PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_options(example_writer PUBLIC ${COMPILE_FLAGS})
target_link_libraries(example_writer PUBLIC pthread rt)

# Example reader

add_executable(example_reader example/reader.cpp)

target_include_directories(example_reader PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_options(example_reader PUBLIC ${COMPILE_FLAGS})
target_link_libraries(example_reader PUBLIC pthread rt)
