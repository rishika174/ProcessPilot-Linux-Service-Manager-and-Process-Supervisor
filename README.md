# ProcessPilot: Linux Service Manager & Process Supervisor

ProcessPilot is a lightweight, modern C++ (C++17) service management and process supervision daemon for Linux. It combines the core capabilities of systems like `systemd` and `supervisord` into a clean, understandable, and extensible architecture.

---

## 🛠 Features

- **Process Supervision**: Full lifecycle management (`STARTING`, `RUNNING`, `CRASHED`, `RESTARTING`, `STOPPED`, `DEAD`).
- **Crash Detection & Auto-Recovery**: Detects crashes via POSIX `waitpid(WNOHANG)` / `SIGCHLD`, applies configurable restart policies (`always`, `on-failure`, `on-abort`, `no`), with anti-flapping backoff.
- **Dependency Management**: DAG-based dependency tracking with cycle detection (Tarjan/DFS) and Kahn's topological sorting for sequenced startup and shutdown.
- **Linux Resource Monitoring**: Live CPU % and RSS/VmSize memory sampling using the Linux `/proc/[pid]/stat` and `/proc/[pid]/status` pseudo-filesystems.
- **Async Signal Handling**: Robust POSIX signal dispatching using the Self-Pipe pattern for `SIGCHLD`, `SIGTERM`, `SIGINT`, and `SIGHUP`.
- **UNIX Domain Socket IPC**: High performance local socket communication (`AF_UNIX`) powering the `processpilot` CLI.
- **INI-Style Configuration**: Familiar systemd-inspired `.service` definitions with environment variable injection and working directory customization.

---

## 📂 Project Structure

```
processpilot/
├── src/
│   ├── main.cpp                     # Daemon entry point
│   ├── daemon/                      # Core orchestrator and event loop
│   │   ├── daemon.hpp
│   │   └── daemon.cpp
│   ├── process/                     # Process fork/exec, lifecycle & status
│   │   ├── process_manager.hpp
│   │   └── process_manager.cpp
│   ├── config/                      # systemd INI configuration parser
│   │   ├── config_parser.hpp
│   │   └── config_parser.cpp
│   ├── monitor/                     # Linux /proc resource telemetry
│   │   ├── resource_monitor.hpp
│   │   └── resource_monitor.cpp
│   ├── dependency/                  # Directed Acyclic Graph & topological sorting
│   │   ├── dependency_graph.hpp
│   │   └── dependency_graph.cpp
│   ├── ipc/                         # UNIX domain socket server & client
│   │   ├── unix_socket.hpp
│   │   └── unix_socket.cpp
│   └── logging/                     # Thread-safe color logger
│       ├── logger.hpp
│       └── logger.cpp
├── cli/
│   └── processpilot_cli.cpp         # CLI binary implementation
├── tests/
│   ├── process_tests.cpp            # Process lifecycle and crash tests
│   ├── config_tests.cpp             # Configuration parsing tests
│   └── dependency_tests.cpp         # DAG and cycle detection tests
├── configs/
│   ├── demo.service                 # Sample service with crash behavior
│   ├── database.service             # Mock PostgreSQL dependency
│   ├── cache.service                # Mock Redis dependency
│   └── web-api.service              # Mock HTTP application server
├── docs/
│   ├── architecture.md              # Technical system design
│   └── requirements.md              # Functional & non-functional requirements
├── CMakeLists.txt                   # CMake build configuration
└── README.md
```

---

## ⚡ Quick Start & Build

### Prerequisites
- Modern Linux distribution (Ubuntu 20.04+, Debian 11+, Fedora 36+, Arch)
- GCC / G++ 9+ or Clang 10+ (supporting C++17)
- CMake 3.16+
- Make or Ninja

### Building the Project
```bash
cd processpilot
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Running the Test Suite
```bash
ctest --test-dir build --output-on-failure
# Or run binaries directly:
./build/process_tests
./build/config_tests
./build/dependency_tests
```

### Starting the Daemon
```bash
./build/processpilotd --config-dir ./configs --socket /tmp/processpilot.sock
```

### Controlling Services with the CLI
```bash
# Check service statuses
./build/processpilot status

# Start a service (auto-starts required dependencies)
./build/processpilot start web-api

# Monitor live CPU and Memory consumption
./build/processpilot monitor

# Visualize dependency DAG hierarchy
./build/processpilot graph

# Simulate unexpected crash by sending SIGSEGV
./build/processpilot kill demo 11

# Inspect output logs
./build/processpilot logs demo 20
```
