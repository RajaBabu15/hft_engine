cmake_minimum_required(VERSION 3.16)
project(hft_engine VERSION 2.0.0 LANGUAGES CXX)

# ============================================================================
# Build Configuration
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

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
    src/core/arm64_clock.cpp
    src/core/admission_control.cpp
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

# All source files for main engine
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${ORDER_SOURCES}
    ${FIX_SOURCES}
    ${MATCHING_SOURCES}
    ${ANALYTICS_SOURCES}
    src/main.cpp
)

# ============================================================================
# Executable Target
# ============================================================================

add_executable(${PROJECT_NAME} ${ALL_SOURCES})


# ============================================================================
# Linking
# ============================================================================

target_link_libraries(${PROJECT_NAME} 
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

