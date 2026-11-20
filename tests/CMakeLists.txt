# Standalone tests for SequenceParsing (no dependency besides the C++ standard library).
#   cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
cmake_minimum_required(VERSION 3.10)
project(SequenceParsingTests CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()
add_executable(SequenceParsingTest
    SequenceParsingTest.cpp
    ../SequenceParsing.cpp)
target_include_directories(SequenceParsingTest PRIVATE ..)
if(MSVC)
    target_compile_options(SequenceParsingTest PRIVATE /W4)
else()
    target_compile_options(SequenceParsingTest PRIVATE -Wall -Wextra)
endif()
add_test(NAME SequenceParsingTest COMMAND SequenceParsingTest)
