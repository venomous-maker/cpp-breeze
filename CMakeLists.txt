cmake_minimum_required(VERSION 4.0)
project(breeze)

set(CMAKE_CXX_STANDARD 17)

add_executable(breeze main.cpp
        include/breeze/core/application.h)
