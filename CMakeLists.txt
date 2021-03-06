cmake_minimum_required(VERSION 2.8)

project(rlite)

# Compiler checks
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")

if(CMAKE_C_COMPILER STREQUAL "CMAKE_C_COMPILER-NOTFOUND")
    message(FATAL_ERROR "Cannot find C compiler")
endif()

if(CMAKE_CXX_COMPILER STREQUAL "CMAKE_CXX_COMPILER-NOTFOUND")
    message(FATAL_ERROR "Cannot find C++ compiler")
endif()

# Prepare compiler flags for C++11
if (CMAKE_VERSION VERSION_LESS "3.1")
    set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")
else()
    set(CMAKE_CXX_STANDARD 11)
endif()

# Some globbing and headers include
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB RLITE_HEADERS "include/rlite/*.h")

message(STATUS "CMAKE_CURRENT_SOURCE_DIR: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "INCLUDE_DIR: ${INCLUDE_DIR}")
message(STATUS "RLITE HEADERS: ${RLITE_HEADERS}")

add_definitions("-Wall -g -Werror")
if(DEBUG STREQUAL "y")
    add_definitions("-DRL_DEBUG")
    # Enable address sanitizer
    set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    message(STATUS "Configuring for debug mode")
endif()

include_directories(${INCLUDE_DIR})

install(FILES ${RLITE_HEADERS} DESTINATION usr/include/rlite)
install(FILES include/rina/cdap.hpp DESTINATION usr/include/rina)
install(FILES "include/rina/api.h" DESTINATION usr/include/rina)

enable_testing()

add_subdirectory(user)
add_subdirectory(tests)
