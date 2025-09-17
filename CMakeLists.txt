cmake_minimum_required(VERSION 3.16)
project(hft_engine VERSION 2.0.0 LANGUAGES CXX)

# ============================================================================
# Build Configuration
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

# Redis Cluster Support Configuration
option(ENABLE_REDIS_CLUSTER "Enable Redis cluster support" OFF)
if(ENABLE_REDIS_CLUSTER)
    message(STATUS "Redis cluster support: ENABLED")
    add_definitions(-DENABLE_REDIS_CLUSTER)
    # Look for hiredis-cluster library
    find_path(HIREDIS_CLUSTER_INCLUDE_DIR hiredis/cluster.h
        HINTS /usr/local/include /opt/homebrew/include)
    find_library(HIREDIS_CLUSTER_LIB hiredis_cluster
        HINTS /usr/local/lib /opt/homebrew/lib)
    
    if(HIREDIS_CLUSTER_INCLUDE_DIR AND HIREDIS_CLUSTER_LIB)
        message(STATUS "hiredis-cluster found: ${HIREDIS_CLUSTER_LIB}")
        include_directories(${HIREDIS_CLUSTER_INCLUDE_DIR})
    else()
        message(FATAL_ERROR "hiredis-cluster library not found but ENABLE_REDIS_CLUSTER=ON")
    endif()
else()
    message(STATUS "Redis cluster support: DISABLED (single Redis mode)")
endif()

# ============================================================================
# Compiler Flags and Optimizations
# ============================================================================

# Extreme optimization flags for ultra-low latency (Clang compatible)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -flto=thin")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -funroll-loops")
# Remove unsupported flags: -funroll-all-loops -finline-limit=10000
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -finline-functions")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffast-math -funsafe-math-optimizations")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fomit-frame-pointer -pthread")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fvectorize -fslp-vectorize")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fwhole-program-vtables")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fstrict-aliasing -fstrict-enums")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fmerge-all-constants")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNUMA_AWARE -DASYNC_OPTIMIZED -DHIGH_FREQUENCY")

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra")

# Platform specific optimizations with CPU-specific tuning
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native -mtune=native")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mavx2 -mfma")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mcpu=native")
    # Apple Silicon specific optimizations (M1/M2/M3) - conservative flags for compatibility
    if(APPLE)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -arch arm64")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -falign-functions=32")
        # Remove problematic LLVM flags that may not be supported
        # set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mllvm -enable-machine-outliner=never")
        # set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mllvm -inline-threshold=10000")
    endif()
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
    src/core/arm64_clock.cpp
    src/core/admission_control.cpp
    src/core/async_logger.cpp
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
    src/analytics/pnl_calculator_simple.cpp
)

# Backtesting
set(BACKTESTING_SOURCES
    src/backtesting/tick_replay.cpp
)

# All source files for main engine (now optimized)
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${ORDER_SOURCES}
    ${FIX_SOURCES}
    ${MATCHING_SOURCES}
    ${ANALYTICS_SOURCES}
    ${BACKTESTING_SOURCES}
    src/main.cpp
)

# ============================================================================
# Executable Target
# ============================================================================

add_executable(${PROJECT_NAME} ${ALL_SOURCES})

# Enable LTO if supported
include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
if(lto_supported)
    message(STATUS "IPO/LTO enabled")
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "IPO/LTO not supported: ${lto_error}")
endif()

# Redis enabled for production use

# Add FIX integration example (commented out - file missing)
# set(FIX_EXAMPLE_SOURCES
#     ${CORE_SOURCES}
#     ${ORDER_SOURCES}
#     ${FIX_SOURCES}
#     ${MATCHING_SOURCES}
#     ${ANALYTICS_SOURCES}
#     ${BACKTESTING_SOURCES}
#     src/fix_integration_example.cpp
# )

# add_executable(fix_integration_example ${FIX_EXAMPLE_SOURCES})

# Add tick data generator utility
add_executable(tick_data_generator src/backtesting/tick_data_generator.cpp)

# Add backtesting runner for demo
set(BACKTEST_RUNNER_SOURCES
    ${CORE_SOURCES}
    ${ORDER_SOURCES}
    ${FIX_SOURCES}
    ${MATCHING_SOURCES}
    ${ANALYTICS_SOURCES}
    ${BACKTESTING_SOURCES}
    src/backtest_runner.cpp
)
add_executable(backtest_runner ${BACKTEST_RUNNER_SOURCES})

# Add concurrency uplift test
set(CONCURRENCY_TEST_SOURCES
    ${CORE_SOURCES}
    ${ORDER_SOURCES}
    ${FIX_SOURCES}
    ${MATCHING_SOURCES}
    ${ANALYTICS_SOURCES}
    ${BACKTESTING_SOURCES}
    src/concurrency_test.cpp
)
add_executable(concurrency_test ${CONCURRENCY_TEST_SOURCES})

# Add tick replay demo
# add_executable(tick_replay_demo src/tick_replay_demo.cpp ${BACKTESTING_SOURCES} ${CORE_SOURCES} ${ORDER_SOURCES} ${MATCHING_SOURCES} ${ANALYTICS_SOURCES})

# Add optimized HFT engine for 10K+ msg/s target
# set(OPTIMIZED_SOURCES
#     ${CORE_SOURCES}
#     ${ORDER_SOURCES}
#     ${FIX_SOURCES}
#     ${MATCHING_SOURCES}
#     ${ANALYTICS_SOURCES}
#     ${BACKTESTING_SOURCES}
#     src/main_optimized.cpp
# )

# add_executable(hft_engine_optimized ${OPTIMIZED_SOURCES})

# ============================================================================
# Linking
# ============================================================================

# Link libraries with optimization support
target_link_libraries(${PROJECT_NAME} 
    ${CMAKE_THREAD_LIBS_INIT}
    /opt/homebrew/lib/libhiredis.dylib
)

# Add hiredis-cluster library if cluster support is enabled
if(ENABLE_REDIS_CLUSTER AND HIREDIS_CLUSTER_LIB)
    target_link_libraries(${PROJECT_NAME} ${HIREDIS_CLUSTER_LIB})
endif()

# Link libraries for backtest runner
target_link_libraries(backtest_runner 
    ${CMAKE_THREAD_LIBS_INIT}
    /opt/homebrew/lib/libhiredis.dylib
)
if(ENABLE_REDIS_CLUSTER AND HIREDIS_CLUSTER_LIB)
    target_link_libraries(backtest_runner ${HIREDIS_CLUSTER_LIB})
endif()

# Link libraries for concurrency test
target_link_libraries(concurrency_test 
    ${CMAKE_THREAD_LIBS_INIT}
    /opt/homebrew/lib/libhiredis.dylib
)
if(ENABLE_REDIS_CLUSTER AND HIREDIS_CLUSTER_LIB)
    target_link_libraries(concurrency_test ${HIREDIS_CLUSTER_LIB})
endif()

# target_link_libraries(fix_integration_example 
#     ${CMAKE_THREAD_LIBS_INIT}
#     /opt/homebrew/lib/libhiredis.dylib
# )

# target_link_libraries(tick_replay_demo 
#     ${CMAKE_THREAD_LIBS_INIT}
#     /opt/homebrew/lib/libhiredis.dylib
# )

# target_link_libraries(hft_engine_optimized 
#     ${CMAKE_THREAD_LIBS_INIT}
#     /opt/homebrew/lib/libhiredis.dylib
# )

# Add OpenMP support if available
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(${PROJECT_NAME} OpenMP::OpenMP_CXX)
    target_link_libraries(backtest_runner OpenMP::OpenMP_CXX)
    target_link_libraries(concurrency_test OpenMP::OpenMP_CXX)
    # target_link_libraries(fix_integration_example OpenMP::OpenMP_CXX)
    # target_link_libraries(hft_engine_optimized OpenMP::OpenMP_CXX)
    message(STATUS "OpenMP support: ENABLED")
else()
    message(STATUS "OpenMP support: DISABLED")
endif()



# ============================================================================
# Custom Targets
# ============================================================================

# Debug targets removed for production optimization

# Run target
add_custom_target(run
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running HFT Engine"
)



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

