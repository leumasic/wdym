cmake_minimum_required(VERSION 4.5)

project(wdym
  VERSION 0.1.0
  HOMEPAGE_URL "https://github.com/leumasic/wdym"
  LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

option(WDYM_BUILD_PYTHON "Build the nanobind Python extension" ON)
option(WDYM_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})

include(FetchContent)

FetchContent_Declare(unordered_dense
  GIT_REPOSITORY https://github.com/martinus/unordered_dense.git
  GIT_TAG        v5.3.3
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL
  SYSTEM
)
FetchContent_MakeAvailable(unordered_dense)

FetchContent_Declare(darts_clone
  GIT_REPOSITORY https://github.com/s-yata/darts-clone.git
  GIT_TAG        v0.32h
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL
  SYSTEM
)
FetchContent_MakeAvailable(darts_clone)

add_library(wdym STATIC
  core/normalization.cpp
  core/tokenizers/byte_level_bpe.cpp
  core/tokenizers/bpe.cpp
)
add_library(wdym::wdym ALIAS wdym)

target_sources(wdym PUBLIC
  FILE_SET CXX_MODULES BASE_DIRS ${PROJECT_SOURCE_DIR}/core FILES
    core/wdym.cppm
    core/utils/containers.cppm
    core/utils/double_array_trie.cppm
    core/normalization.cppm
    core/tokenizer.cppm
    core/tokenizers/bpe.cppm
    core/tokenizers/byte_level_bpe.cppm)
target_compile_features(wdym PUBLIC cxx_std_23)
target_compile_options(wdym PRIVATE -Wall -Wextra)
# BUILD_INTERFACE keeps this header-only dependency out of the installed wdymTargets export
target_link_libraries(wdym PRIVATE $<BUILD_INTERFACE:unordered_dense::unordered_dense>)
target_include_directories(wdym SYSTEM PRIVATE $<BUILD_INTERFACE:${darts_clone_SOURCE_DIR}/include>)
set_target_properties(wdym PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (WDYM_BUILD_PYTHON)
  find_package(Python 3.12 COMPONENTS Interpreter Development.Module REQUIRED
    OPTIONAL_COMPONENTS Development.SABIModule)

  # Detect the installed nanobind package and import it into CMake
  execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT)
  find_package(nanobind CONFIG REQUIRED)

  nanobind_add_module(wdym_ext NB_STATIC STABLE_ABI NOMINSIZE bindings/wdymBinding.cpp)
  target_link_libraries(wdym_ext PRIVATE wdym::wdym)
  install(TARGETS wdym_ext LIBRARY DESTINATION wdym)
endif()

if (WDYM_BUILD_TESTS AND NOT SKBUILD)
  enable_testing()
  add_subdirectory(tests)
endif()

if (NOT SKBUILD)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS wdym EXPORT wdymTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wdym)
  install(EXPORT wdymTargets NAMESPACE wdym::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wdym
    CXX_MODULES_DIRECTORY modules)

  file(WRITE ${PROJECT_BINARY_DIR}/wdymConfig.cmake
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/wdymTargets.cmake\")\n")
  write_basic_package_version_file(${PROJECT_BINARY_DIR}/wdymConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
  install(FILES ${PROJECT_BINARY_DIR}/wdymConfig.cmake ${PROJECT_BINARY_DIR}/wdymConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wdym)
endif()
