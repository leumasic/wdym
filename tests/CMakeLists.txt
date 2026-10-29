include(FetchContent)

FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.16.1
  GIT_SHALLOW    TRUE
  SYSTEM
)

# Build Catch2 with the same standard as the tests (scoped to this directory)
set(CMAKE_CXX_STANDARD 23)
FetchContent_MakeAvailable(Catch2)

add_executable(wdym_tests
  tokenizer_tests.cpp
)
target_link_libraries(wdym_tests PRIVATE wdym::wdym Catch2::Catch2WithMain)
target_compile_options(wdym_tests PRIVATE -Wall -Wextra)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(wdym_tests)
