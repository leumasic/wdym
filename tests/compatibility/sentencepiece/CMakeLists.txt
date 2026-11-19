cmake_minimum_required(VERSION 4.5)
project(wdym_sentencepiece_compatibility LANGUAGES CXX)

set(WDYM_BUILD_TESTS OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../.. wdym)

add_executable(compare_wdym compare_wdym.cpp)
target_link_libraries(compare_wdym PRIVATE wdym::wdym)
