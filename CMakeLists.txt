cmake_minimum_required(VERSION 3.16)
project(ring_allreduce LANGUAGES CXX)

# C++ standard.
#
# NOTE: the build spec for this project states "C++20" as a non-negotiable
# up front, but also says "C++17" in its build-system section. We resolved
# this in favor of C++20 (the section explicitly flagged non-negotiable).
# See docs/DESIGN_DECISIONS.md for the full rationale.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(MPI REQUIRED)

# --- Options -----------------------------------------------------------
# Warnings are always emitted (-Wall -Wextra -Wpedantic); whether they are
# fatal is controlled separately so local dev builds are not blocked by a
# single warning while CI still enforces zero warnings (see .github/workflows/ci.yml).
option(RING_ALLREDUCE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (enabled in CI)" OFF)

# ENABLE_SANITIZERS wires -fsanitize=address,undefined into test binaries.
option(ENABLE_SANITIZERS "Build test binaries with ASan+UBSan" OFF)

# Allow a from-scratch checkout to skip building tests if desired.
option(RING_ALLREDUCE_BUILD_TESTS "Build the test suite" ON)

# --- Shared compiler-option helper --------------------------------------
# Applied to every target in this project (library, apps, tests) so the
# "-Wall -Wextra -Wpedantic, zero warnings" requirement is enforced uniformly
# rather than bolted onto individual targets ad hoc.
function(ring_allreduce_apply_common_options target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  if(RING_ALLREDUCE_WARNINGS_AS_ERRORS)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
  if(ENABLE_SANITIZERS)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# --- Core library --------------------------------------------------------
# Everything under include/ring_allreduce + src/ compiles into one static
# library that apps/ and tests/ both link against.
add_library(ring_allreduce_core STATIC
  src/ring_allreduce.cpp
  src/chunking.cpp
)
add_library(ring_allreduce::core ALIAS ring_allreduce_core)

target_include_directories(ring_allreduce_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ring_allreduce_core PUBLIC MPI::MPI_CXX)
ring_allreduce_apply_common_options(ring_allreduce_core)

add_subdirectory(apps)

if(RING_ALLREDUCE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
