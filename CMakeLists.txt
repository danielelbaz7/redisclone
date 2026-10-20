cmake_minimum_required(VERSION 4.0)
project(redisclone)

set(CMAKE_CXX_STANDARD 23)

find_package(Threads REQUIRED)

# everything except the server loop, shared by the server and the tests
add_library(redisclone_core
        RespParser.cpp
        RespParser.h
        Dispatcher.cpp
        Dispatcher.h
        KeyValueStore.cpp
        KeyValueStore.h
        AofLog.cpp
        AofLog.h)
target_include_directories(redisclone_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(redisclone_core PUBLIC Threads::Threads)

add_executable(redisclone main.cpp
        Server.cpp
        Server.h)
target_link_libraries(redisclone PRIVATE redisclone_core)

enable_testing()
add_executable(redisclone_tests tests/tests.cpp)
target_link_libraries(redisclone_tests PRIVATE redisclone_core)
add_test(NAME redisclone_tests COMMAND redisclone_tests)
