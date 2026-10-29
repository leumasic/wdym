include(FetchContent)

FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.16.1
  GIT_SHALLOW    TRUE
  SYSTEM
)

set(CMAKE_CXX_STANDARD 26)
FetchContent_MakeAvailable(Catch2)

file(GLOB test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp)

add_executable(wdym_tests ${test_sources})
target_link_libraries(wdym_tests PRIVATE wdym::wdym Catch2::Catch2WithMain)
target_compile_options(wdym_tests PRIVATE -Wall -Wextra)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(wdym_tests)
