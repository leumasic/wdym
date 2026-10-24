cmake_minimum_required(VERSION 3.24)

project(wdym VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

option(WDYM_BUILD_PYTHON "Build the nanobind Python extension" ON)

include(GNUInstallDirs)

add_library(wdym STATIC
  core/tokenizers/byte_level_bpe.cpp
  core/tokenizers/bpe.cpp
)
add_library(wdym::wdym ALIAS wdym)

target_include_directories(wdym PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(wdym PUBLIC cxx_std_23)
set_target_properties(wdym PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (WDYM_BUILD_PYTHON)
  find_package(Python 3.12 COMPONENTS Interpreter Development.Module REQUIRED
    OPTIONAL_COMPONENTS Development.SABIModule)

  # Detect the installed nanobind package and import it into CMake
  execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT)
  find_package(nanobind CONFIG REQUIRED)

  nanobind_add_module(wdym_ext NB_STATIC STABLE_ABI bindings/wdymBinding.cpp)
  target_link_libraries(wdym_ext PRIVATE wdym::wdym)
  install(TARGETS wdym_ext LIBRARY DESTINATION wdym)
endif()

if (NOT SKBUILD)
  include(CMakePackageConfigHelpers)

  install(TARGETS wdym EXPORT wdymTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT wdymTargets NAMESPACE wdym::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wdym)

  file(WRITE ${PROJECT_BINARY_DIR}/wdymConfig.cmake
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/wdymTargets.cmake\")\n")
  write_basic_package_version_file(${PROJECT_BINARY_DIR}/wdymConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
  install(FILES ${PROJECT_BINARY_DIR}/wdymConfig.cmake ${PROJECT_BINARY_DIR}/wdymConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wdym)
endif()
