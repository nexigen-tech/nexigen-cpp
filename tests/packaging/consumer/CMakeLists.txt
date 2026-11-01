cmake_minimum_required(VERSION 3.25)
project(nexigen_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nexigen 0.1 CONFIG REQUIRED)

add_executable(nexigen_consumer main.cpp)
target_link_libraries(nexigen_consumer PRIVATE nexigen::logging nexigen::metrics)
