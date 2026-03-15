cmake_minimum_required(VERSION 3.31)
project(zconfig CXX)

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(zconfig ${SOURCES})
 
target_include_directories(zconfig PRIVATE include)
target_compile_options(zconfig PRIVATE -Wall -Wextra -Werror -O2)
target_link_libraries(zconfig ftxui-component ftxui-dom ftxui-screen)
