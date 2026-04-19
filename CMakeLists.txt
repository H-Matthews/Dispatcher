cmake_minimum_required(VERSION 3.16)
project(SignalDispatcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(signal_dispatcher
    main.cpp
    SignalDispatcher.cpp
)

target_include_directories(signal_dispatcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})