project(shq_test)

# Sender file

add_executable(test_sender test/sender.cpp)

target_include_directories(test_sender PUBLIC ${PROJECT_SOURCE_DIR})

target_compile_options(test_sender PUBLIC
    -std=c++11
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wunreachable-code)

target_link_libraries(test_sender PUBLIC pthread rt)

# Reader file

add_executable(test_reader test/reader.cpp)

target_include_directories(test_reader PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(test_reader PUBLIC pthread rt)

target_compile_options(test_reader PUBLIC
    -std=c++11
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wunreachable-code)
