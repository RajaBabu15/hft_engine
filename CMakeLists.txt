cmake_minimum_required(VERSION 3.16)
project(HFTEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# --- External Dependencies ---
find_package(Boost 1.74 REQUIRED COMPONENTS system thread)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)

# --- Vendored WebSocket++ (compatible v0.8.2) ---
add_library(websocketpp INTERFACE)
# CORRECTED: Point to the directory CONTAINING the websocketpp folder
target_include_directories(websocketpp INTERFACE
        ${CMAKE_SOURCE_DIR}/external/websocketpp
)

# --- Core Engine Library ---
add_library(hft_core
        src/matching_engine.cpp
        src/order_book.cpp
        src/websocket_client.cpp
)

# Public headers for hft_core
target_include_directories(hft_core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link in Boost, OpenSSL, JSON, and our vendored wspp
target_link_libraries(hft_core
        PUBLIC
        Boost::system
        Boost::thread
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        websocketpp
)

# --- Main Executable ---
add_executable(hft_engine
        src/main.cpp
)
target_link_libraries(hft_engine
        PRIVATE
        hft_core
)

# --- Testing Setup ---
enable_testing()
add_subdirectory(tests)
