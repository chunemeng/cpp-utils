cmake_minimum_required(VERSION 3.22.1)

project(cpp_utils)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "C compiler version: ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "C++ compiler version: ${CMAKE_CXX_COMPILER_VERSION}")


OPTION(ENABLE_TEST "Enable test" ON)
OPTION(ENABLE_BENCHMARK "Enable benchmark" ON)

OPTION(ENABLE_DEBUG "Enable debug" OFF)
OPTION(ENABLE_DEBUG_INFO "Enable debug info" OFF)
# Sanitizers(enable once at a time)
OPTION(ENABLE_ASAN "Enable AddressSanitizer" OFF)
OPTION(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
OPTION(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if (ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -fsanitize=leak")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address -fsanitize=leak")
endif ()

if (ENABLE_UBSAN)
    message(STATUS "UndefinedBehaviorSanitizer enabled")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
endif ()

if (ENABLE_TSAN)
    message(STATUS "ThreadSanitizer enabled")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif ()

if (UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif ()

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-interference-size")

if (ENABLE_DEBUG)
    message(STATUS "Debug mode enabled")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -w")
else ()
    message(STATUS "Release mode enabled")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -march=native")
endif ()

if (ENABLE_DEBUG_INFO)
    message(STATUS "Debug info enabled")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
endif ()

include_directories(include)

if (ENABLE_TEST)
    message(STATUS "Test enabled")
    include(extern/gtest/googletest.cmake)
    add_subdirectory(test)
    enable_testing()
endif ()

if (ENABLE_BENCHMARK)
    message(STATUS "Benchmark enabled")
    include(extern/benchmark/benchmark.cmake)
    add_subdirectory(bench)
endif ()






