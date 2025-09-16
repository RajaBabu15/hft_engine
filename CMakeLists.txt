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

# ============================================================================
# Source Files
# ============================================================================

# Core components
set(CORE_SOURCES
    src/core/clock.cpp
)

# Order management
set(ORDER_SOURCES
    src/order/order.cpp
    src/order/price_level.cpp
    src/order/order_book.cpp
)

# All source files
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${ORDER_SOURCES}
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

# Python demo target (if Python is available)
if(Python3_FOUND)
    message(STATUS "Python3 found: ${Python3_EXECUTABLE}")
    add_custom_target(run_python
        COMMAND ${Python3_EXECUTABLE} -c "print('Python integration would go here')"
        COMMENT "Running Python integration demo"
    )
else()
    message(WARNING "Python3 not found - Python integration disabled")
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

message(STATUS "=== Enhanced HFT Trading Engine v${PROJECT_VERSION} ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ flags: ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")
message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")

if(Python3_FOUND)
    message(STATUS "Python support: ON")
    message(STATUS "Python executable: ${Python3_EXECUTABLE}")
else()
    message(STATUS "Python support: OFF")
endif()

message(STATUS "Features: Modular Design, Lock-free Queues, High-precision Timing")