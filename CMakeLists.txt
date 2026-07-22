cmake_minimum_required(VERSION 3.16)
project(TradeVerse VERSION 0.2 LANGUAGES CXX)

# ============================================================================
# C++20 Standard
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Compiler Flags
# ============================================================================
if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -O2 -march=native)
endif()

# ============================================================================
# Find Dependencies
# ============================================================================
# ZeroMQ — look in MSYS2 paths on Windows
if(WIN32 AND EXISTS "C:/msys64/ucrt64")
    list(APPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64")
    include_directories("C:/msys64/ucrt64/include")
    link_directories("C:/msys64/ucrt64/lib")
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ZMQ QUIET libzmq)
endif()

# ============================================================================
# Risk Engine Executable
# ============================================================================
add_executable(tradeverse_server
    src/cpp/server.cpp
    src/cpp/greeks.cpp
    src/cpp/portfolio.cpp
)

# Link ZeroMQ
if(ZMQ_FOUND)
    target_include_directories(tradeverse_server PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_link_libraries(tradeverse_server PRIVATE ${ZMQ_LIBRARIES})
else()
    # Fallback: link directly (works with MSYS2)
    target_link_libraries(tradeverse_server PRIVATE zmq)
endif()

# Threading support
find_package(Threads REQUIRED)
target_link_libraries(tradeverse_server PRIVATE Threads::Threads)

# ============================================================================
# Output
# ============================================================================
set_target_properties(tradeverse_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    OUTPUT_NAME "server"
)

message(STATUS "TradeVerse Risk Engine v${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
