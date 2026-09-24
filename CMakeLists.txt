cmake_minimum_required(VERSION 3.16)
project(ProcessPilot
    VERSION 1.0.0
    DESCRIPTION "Lightweight Linux Service Manager & Process Supervisor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wshadow -O2 -pthread")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Common source files
set(CORE_SOURCES
    src/logging/logger.cpp
    src/config/config_parser.cpp
    src/dependency/dependency_graph.cpp
    src/monitor/resource_monitor.cpp
    src/process/process_manager.cpp
    src/ipc/unix_socket.cpp
)

# Daemon executable (processpilotd)
add_executable(processpilotd
    src/main.cpp
    src/daemon/daemon.cpp
    ${CORE_SOURCES}
)
target_link_libraries(processpilotd pthread)

# CLI executable (processpilot)
add_executable(processpilot
    cli/processpilot_cli.cpp
    src/ipc/unix_socket.cpp
    src/logging/logger.cpp
)
target_link_libraries(processpilot pthread)

# Test executables
enable_testing()

add_executable(process_tests
    tests/process_tests.cpp
    ${CORE_SOURCES}
)
target_link_libraries(process_tests pthread)
add_test(NAME ProcessManagerTests COMMAND process_tests)

add_executable(config_tests
    tests/config_tests.cpp
    ${CORE_SOURCES}
)
target_link_libraries(config_tests pthread)
add_test(NAME ConfigParserTests COMMAND config_tests)

add_executable(dependency_tests
    tests/dependency_tests.cpp
    ${CORE_SOURCES}
)
target_link_libraries(dependency_tests pthread)
add_test(NAME DependencyGraphTests COMMAND dependency_tests)

message(STATUS "ProcessPilot configured successfully for Linux C++17 target.")
