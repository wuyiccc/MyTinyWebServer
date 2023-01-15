cmake_minimum_required(VERSION 3.24)
project(NkWebServer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS -pthread)

add_executable(NkWebServer main.cpp locker.h threadpool.h http_conn.cpp http_conn.h)
