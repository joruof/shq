project(shq_test)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sender file

add_executable(test_sender test/sender.cpp)

target_include_directories(test_sender PUBLIC ${PROJECT_SOURCE_DIR})

target_compile_options(test_sender PUBLIC
    -std=c++11
    -Wall
    -Wextra
    -Wpedantic
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
    -Wunreachable-code)

# File lock 

add_executable(test_file_lock test/file_lock.cpp)

target_include_directories(test_file_lock PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(test_file_lock PUBLIC pthread rt)

target_compile_options(test_file_lock PUBLIC
    -std=c++11
    -Wall
    -Wextra
    -Wpedantic
    -Wunreachable-code)

# Futex cond var

add_executable(test_futex_cond_var test/futex_cond_var.cpp)

target_include_directories(test_futex_cond_var PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(test_futex_cond_var PUBLIC pthread rt)
        
target_compile_options(test_futex_cond_var PUBLIC
    -std=c++11
    -Wall
    -Wextra
    -Wpedantic
    -Wunreachable-code)

# Deleter

add_executable(test_deleter test/deleter.cpp)

target_include_directories(test_deleter PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(test_deleter PUBLIC pthread rt)
        
target_compile_options(test_deleter PUBLIC
    -std=c++11
    -Wall
    -Wextra
    -Wpedantic
    -Wunreachable-code)
