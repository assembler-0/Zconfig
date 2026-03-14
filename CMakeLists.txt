cmake_minimum_required(VERSION 3.31)
project(untitled)

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(untitled ${SOURCES})
 
target_include_directories(untitled PRIVATE include)
target_link_libraries(untitled fmt ftxui-component ftxui-dom ftxui-screen)
