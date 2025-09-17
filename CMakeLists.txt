cmake_minimum_required(VERSION 3.16)
project(hft_engine VERSION 2.0.0 LANGUAGES CXX)

# ============================================================================
# Build Configuration
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter)

# ============================================================================
# Compiler Flags and Optimizations
# ============================================================================

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -funroll-loops -finline-functions -ffast-math")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra")

# Platform specific optimizations
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native -mtune=native")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mcpu=native")
endif()

# ============================================================================
# Include Directories
# ============================================================================

include_directories(include)
include_directories(/opt/homebrew/include)

# ============================================================================
# Source Files
# ============================================================================

# Core components
set(CORE_SOURCES
    src/core/redis_client.cpp
    src/core/simple_clock.cpp
)

# Order management
set(ORDER_SOURCES
    src/order/order.cpp
    src/order/price_level.cpp
    src/order/order_book.cpp
)

# FIX protocol
set(FIX_SOURCES
    src/fix/fix_parser.cpp
)

# Matching engine
set(MATCHING_SOURCES
    src/matching/matching_engine.cpp
)

# Analytics
set(ANALYTICS_SOURCES
    src/analytics/pnl_calculator.cpp
)

# Testing framework
set(TESTING_SOURCES
    src/testing/stress_test.cpp
)

# Comprehensive HFT Engine sources
set(ENGINE_SOURCES
    src/engine/hft_engine.cpp
)

# All source files
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${ORDER_SOURCES}
    src/simple_demo.cpp
    src/main.cpp
)

# ============================================================================
# Executable Target
# ============================================================================

add_executable(${PROJECT_NAME} ${ALL_SOURCES})

# Comprehensive HFT Engine Demonstration
add_executable(hft_engine_demo
    run_integrated_hft_engine.cpp
    ${CORE_SOURCES}
    ${ORDER_SOURCES}
    ${FIX_SOURCES}
    ${MATCHING_SOURCES}
    ${ANALYTICS_SOURCES}
    ${TESTING_SOURCES}
    ${ENGINE_SOURCES}
)

# HFT Performance Benchmark
add_executable(benchmark 
    src/benchmark.cpp
    ${CORE_SOURCES}
    ${ORDER_SOURCES}
)

# ============================================================================
# Linking
# ============================================================================

target_link_libraries(${PROJECT_NAME} 
    ${CMAKE_THREAD_LIBS_INIT}
    /opt/homebrew/lib/libhiredis.dylib
)

# ARM64 and macOS specific optimizations for benchmark
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    target_compile_definitions(benchmark PRIVATE __aarch64__=1)
    target_compile_options(benchmark PRIVATE 
        -mcpu=native
        -mtune=native
        -march=armv8-a+simd
        -O3
        -DNDEBUG
        -ffast-math
        -funroll-loops
    )
endif()

if(APPLE)
    target_compile_definitions(benchmark PRIVATE __APPLE__=1)
endif()

target_link_libraries(benchmark 
    ${CMAKE_THREAD_LIBS_INIT}
    /opt/homebrew/lib/libhiredis.dylib
)

# Link comprehensive HFT demo
target_link_libraries(hft_engine_demo
    ${CMAKE_THREAD_LIBS_INIT}
    /opt/homebrew/lib/libhiredis.dylib
)

# ============================================================================
# Custom Targets
# ============================================================================

# Run target
add_custom_target(run
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running HFT Engine"
)

add_custom_target(run_benchmark
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/benchmark
    DEPENDS benchmark
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running HFT Performance Benchmark"
)

add_custom_target(run_demo
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/hft_engine_demo
    DEPENDS hft_engine_demo
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running Comprehensive HFT Engine Demo"
)

add_custom_target(run_stress_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/hft_engine_demo 2
    DEPENDS hft_engine_demo
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running HFT Stress Test (100k+ msg/sec)"
)

# Python demo target (if Python is available)
if(Python3_FOUND)
    add_custom_target(run_python
        COMMAND ${Python3_EXECUTABLE} example.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running Python example"
    )
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# ============================================================================
# Configuration Summary
# ============================================================================

message(STATUS "HFT Trading Engine v${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "System: ${CMAKE_SYSTEM_PROCESSOR}")

if(Python3_FOUND)
    message(STATUS "Python support: ON")
else()
    message(STATUS "Python support: OFF")
endif()
