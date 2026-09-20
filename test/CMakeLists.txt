# ---------------------------------------------------------------------------
# mayara_pi - unit tests for the wx-free core.
#
# A STANDALONE CMake project, deliberately not add_subdirectory()'d from the
# top-level CMakeLists: that one is OpenCPN's FE2 template, which needs
# wxWidgets and the OpenCPN plugin API before it will even configure. Every
# source tested here is free of both, so the tests configure and build with
# nothing but a C++17 compiler:
#
#   cmake -B build-test -S test && cmake --build build-test && ctest --test-dir build-test
#
# or just `make test` / `make coverage` from the repository root.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.15)
project(mayara_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(MAYARA_COVERAGE "Instrument the tests for coverage reporting" OFF)
option(MAYARA_SANITIZE "Build the tests with address + UB sanitizers" OFF)

set(MAYARA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The units under test. Keep this list to sources that are free of wx and of
# the OpenCPN plugin API -- anything else drags a GUI toolkit into what should
# stay a plain compiler-only build.
set(MAYARA_UNIT_SRCS
    ${MAYARA_ROOT}/src/RadarMessage.cpp
    ${MAYARA_ROOT}/src/RadarState.cpp
    ${MAYARA_ROOT}/src/RadarPalette.cpp
    ${MAYARA_ROOT}/src/RadarControls.cpp
)

set(MAYARA_TEST_SRCS
    main.cpp
    test_radar_message.cpp
    test_radar_state.cpp
    test_radar_palette.cpp
    test_radar_controls.cpp
)

add_executable(mayara_tests ${MAYARA_TEST_SRCS} ${MAYARA_UNIT_SRCS})

target_include_directories(
  mayara_tests PRIVATE ${MAYARA_ROOT}/include ${MAYARA_ROOT}/third_party
                       ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(mayara_tests PRIVATE Threads::Threads)

if(MSVC)
  target_compile_options(mayara_tests PRIVATE /W4 /utf-8 /permissive-)
  target_compile_definitions(mayara_tests PRIVATE _USE_MATH_DEFINES)
else()
  target_compile_options(mayara_tests PRIVATE -Wall -Wextra)
endif()

if(MAYARA_SANITIZE)
  if(MSVC)
    message(WARNING "MAYARA_SANITIZE is not supported with MSVC; ignoring")
  else()
    target_compile_options(mayara_tests PRIVATE -fsanitize=address,undefined
                                                -fno-omit-frame-pointer -g)
    target_link_options(mayara_tests PRIVATE -fsanitize=address,undefined)
  endif()
endif()

if(MAYARA_COVERAGE)
  # test/coverage.sh has to pick the matching report toolchain (llvm-cov vs
  # gcov), and CMake is the only thing here that knows which compiler was
  # used. Hand it over in a file rather than making the script guess or dig
  # around in CMake's own internals.
  file(WRITE ${CMAKE_BINARY_DIR}/compiler-id.txt "${CMAKE_CXX_COMPILER_ID}")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Source-based coverage: llvm-profdata + llvm-cov, driven by
    # test/coverage.sh.
    target_compile_options(mayara_tests PRIVATE -fprofile-instr-generate
                                                -fcoverage-mapping -O0 -g)
    target_link_options(mayara_tests PRIVATE -fprofile-instr-generate)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(mayara_tests PRIVATE --coverage -O0 -g)
    target_link_options(mayara_tests PRIVATE --coverage)
  else()
    message(FATAL_ERROR "MAYARA_COVERAGE needs Clang or GCC, not "
                        "${CMAKE_CXX_COMPILER_ID}")
  endif()
endif()

enable_testing()

# One ctest entry per doctest suite, so `ctest -R RadarState` works and a
# failure names the area rather than just "the tests".
foreach(suite RadarMessage RadarState RadarPalette RadarControls)
  add_test(NAME ${suite} COMMAND mayara_tests --test-suite=${suite}
                                 --no-intro=true --force-colors=false)
endforeach()

# Safety net for the list above: if a suite is renamed and its foreach() entry
# goes stale, the filtered run silently passes with nothing executed. This one
# runs the whole binary, so no test can be dropped unnoticed.
add_test(NAME all COMMAND mayara_tests --force-colors=false)
