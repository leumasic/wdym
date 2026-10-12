cmake_minimum_required(VERSION 3.24)

project(wdym LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(DEV_MODULE Development.Module)
find_package(Python 3.10 COMPONENTS Interpreter ${DEV_MODULE} REQUIRED)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Detect the installed nanobind package and import it into CMake
execute_process(
  COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT)
find_package(nanobind CONFIG REQUIRED)

# Link compile_commands.json into the repository root, where clangd looks for it. CMake
# writes the file after configuring, so the link may dangle until then. Where links can't
# be made (Windows without developer mode), the copy-on-error fallback leaves nothing:
# point clangd at build/ instead (--compile-commands-dir=build). Wheel builds (SKBUILD)
# use a temporary build directory, so linking to it would leave a dead link behind.
if(NOT SKBUILD AND NOT PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  file(CREATE_LINK "${PROJECT_BINARY_DIR}/compile_commands.json" "${PROJECT_SOURCE_DIR}/compile_commands.json"
    SYMBOLIC RESULT link_result)
  if(NOT link_result EQUAL 0)
    message(STATUS "Couldn't link compile_commands.json into the repository root (${link_result}); it's in ${PROJECT_BINARY_DIR}")
  endif()
endif()

nanobind_add_module(wdym_ext ./core/wdymBinding.cpp ./core/tokenizer.cpp)
install(TARGETS wdym_ext LIBRARY DESTINATION wdym)
