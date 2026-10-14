# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(frag VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_executable(frag
  src/main.cpp
  src/config.cpp
  src/wine.cpp
)
target_compile_definitions(frag PRIVATE FRAG_VERSION="${PROJECT_VERSION}")
target_compile_options(frag PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS frag RUNTIME DESTINATION bin)

option(FRAG_BUILD_TESTS "Build the Windows test programs and register CTest tests" ON)
if(FRAG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
