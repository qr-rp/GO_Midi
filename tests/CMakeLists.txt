cmake_minimum_required(VERSION 3.25)
project(GO_Midi_Planner_Tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(transpose_planner_tests
    TransposePlannerTests.cpp
    ../src/core/TransposePlanner.cpp
)

target_include_directories(transpose_planner_tests PRIVATE ../src)

enable_testing()
add_test(NAME transpose_planner_tests COMMAND transpose_planner_tests)
