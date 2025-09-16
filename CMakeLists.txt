cmake_minimum_required(VERSION 3.16)
project(HftTradingEngine VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags for high performance
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -Wall -Wextra -Wpedantic -fsanitize=address")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native -flto")

# Additional optimization flags for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -funroll-loops -finline-functions -ffast-math")
endif()

# Platform-specific optimizations
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2 -mavx -mavx2")
endif()

# Find required packages
find_package(Threads REQUIRED)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/hft_engine.cpp
)

# Create the main executable
add_executable(hft_engine ${SOURCES})

# Link libraries
target_link_libraries(hft_engine 
    PRIVATE 
    Threads::Threads
)

# Set properties for the executable
set_target_properties(hft_engine PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(hft_engine PRIVATE -fno-omit-frame-pointer)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(hft_engine PRIVATE -fno-omit-frame-pointer)
endif()

# Add custom target for running the application
add_custom_target(run
    COMMAND $<TARGET_FILE:hft_engine>
    DEPENDS hft_engine
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    COMMENT "Running HFT Trading Engine"
)

# Performance profiling target
add_custom_target(profile
    COMMAND perf record -g $<TARGET_FILE:hft_engine>
    DEPENDS hft_engine
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    COMMENT "Profiling HFT Trading Engine with perf"
)

# Installation
install(TARGETS hft_engine
    RUNTIME DESTINATION bin
)

# Create a config file for easy running
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/config.txt.in"
    "${CMAKE_BINARY_DIR}/bin/config.txt"
    @ONLY
)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ flags: ${CMAKE_CXX_FLAGS}")
message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")