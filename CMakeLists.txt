cmake_minimum_required(VERSION 3.11)
project(mapcrafter)

option(OPT_USE_BOOST_THREAD "Uses boost thread instead of C++11 threads" OFF)
option(OPT_SKIP_TESTS "Skip compiling the boost unittests" OFF)
option(OPT_LINK_DEPS_STATICALLY "Links all dependencies (libpng, libjpeg, boost...) statically" OFF)
option(OPT_LINK_BOOST_STATICALLY "Links boost statically" OFF)
option(OPT_AUTO_SIMD "If supported, generate function variants with support for multiple SIMD levels. Disable this if compiling for a specific target architecture (e.g. -march=native)" ON)

set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)

if(OPT_LINK_DEPS_STATICALLY)
    set(OPT_LINK_BOOST_STATICALLY ON)
endif()

add_definitions(-Wall)

set(CMAKE_CXX_STANDARD 17)

if(OPT_LINK_BOOST_STATICALLY)
    set(Boost_USE_STATIC_LIBS ON)
    # we need zlib to link boost iostreams statically
    find_package(ZLIB REQUIRED)
    include_directories(${ZLIB_INCLUDE_DIRS})
endif()

find_package(Boost COMPONENTS iostreams system filesystem program_options REQUIRED)
if(OPT_USE_BOOST_THREAD)
    find_package(Boost COMPONENTS thread REQUIRED)
else()
    find_package(Threads REQUIRED)
    include_directories(${Threads_INCLUDE_DIRS})
endif()
if(NOT OPT_SKIP_TESTS)
    find_package(Boost COMPONENTS unit_test_framework)
    if(NOT Boost_UNIT_TEST_FRAMEWORK_FOUND)
        set(OPT_SKIP_TESTS ON)
        message("Boost Unit Test Framework not found. Skipping the tests.")
    endif()
endif()
include_directories(${Boost_INCLUDE_DIRS})

find_package(PNG REQUIRED)
include_directories(${PNG_INCLUDE_DIRS})

find_package(JPEG REQUIRED)
# ${JPEG_INCLUDE_DIRS} somehow doesn't work
include_directories(${JPEG_INCLUDE_DIR})

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/src")
