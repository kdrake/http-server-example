cmake_minimum_required(VERSION 3.4)
project(http_server_example)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")

find_package(Boost REQUIRED COMPONENTS regex)
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR})

set(SOURCE_FILES server.cpp)
add_executable(final ${SOURCE_FILES})

target_link_libraries(final ${Boost_LIBRARIES})
