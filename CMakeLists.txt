cmake_minimum_required(VERSION 4.0)
project(redisclone)

set(CMAKE_CXX_STANDARD 23)

add_executable(redisclone main.cpp
        Server.cpp
        Server.h
        RespParser.cpp
        RespParser.h
        Dispatcher.cpp
        Dispatcher.h)
