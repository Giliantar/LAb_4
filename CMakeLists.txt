cmake_minimum_required(VERSION 3.20)
project(Lab4 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Lab4Sender Sender.cpp)
add_executable(Lab4Receiver Receiver.cpp)

add_dependencies(Lab4Receiver Lab4Sender)
