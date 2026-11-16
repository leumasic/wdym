cmake_minimum_required(VERSION 4.5)
project(wdym_benchmarks LANGUAGES CXX)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. wdym)

add_executable(train_wdym train_wdym.cpp)
target_link_libraries(train_wdym PRIVATE wdym::wdym)

add_executable(encode_wdym encode_wdym.cpp)
target_link_libraries(encode_wdym PRIVATE wdym::wdym)
