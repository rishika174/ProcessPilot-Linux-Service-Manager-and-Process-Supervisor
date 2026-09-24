# ProcessPilot - System Requirements Specification

## 1. Functional Requirements (FR)

### FR-1: Service Configuration
- **FR-1.1**: The system must parse configuration files formatted in INI-style syntax (`.service`).
- **FR-1.2**: Must support configuration directives: `Description`, `After`, `Requires`, `ExecStart`, `ExecStop`, `WorkingDirectory`, `User`, `Restart`, `RestartSec`, `MaxRestarts`, `RestartWindowSec`, `AutoStart`, `Environment`.
- **FR-1.3**: Must reject invalid configurations lacking `ExecStart` or containing malformed syntax.

### FR-2: Process Lifecycle Supervision
- **FR-2.1**: Daemon must fork and spawn background child processes with environment inheritance.
- **FR-2.2**: Daemon must capture stdout and stderr non-blockingly into circular in-memory buffers and log files.
- **FR-2.3**: Daemon must cleanly stop processes using `SIGTERM`, escalating to `SIGKILL` after a configurable timeout (default 5s).
- **FR-2.4**: Daemon must reap zombie processes asynchronously without blocking via `waitpid(WNOHANG)`.

### FR-3: Crash Detection & Automatic Recovery
- **FR-3.1**: Must distinguish between intentional shutdown and unexpected crashes (non-zero exit status or fatal signal).
- **FR-3.2**: Must enforce restart policies (`always`, `on-failure`, `on-abort`, `no`).
- **FR-3.3**: Must apply backoff delay (`RestartSec`) before re-executing failed service.
- **FR-3.4**: Must detect rapid crash loops (flapping) and transition services to `DEAD` if crashes exceed `MaxRestarts` in `RestartWindowSec`.

### FR-4: Dependency Orchestration
- **FR-4.1**: Must construct a Directed Acyclic Graph (DAG) representing service relationships.
- **FR-4.2**: Must identify cyclic dependencies and alert administrator with full cycle path.
- **FR-4.3**: Must compute deterministic boot order using topological sorting.
- **FR-4.4**: Must warn or prevent stopping a service if active services require it.

### FR-5: Resource Monitoring
- **FR-5.1**: Must periodically poll `/proc/[pid]/stat` and compute real-time CPU % across time intervals.
- **FR-5.2**: Must extract physical memory (RSS) and virtual memory (VmSize) in human-readable units.
- **FR-5.3**: Must gracefully handle process death without memory corruption or segfaults.

### FR-6: Inter-Process Communication & CLI
- **FR-6.1**: Must expose a UNIX domain socket (`AF_UNIX`) for local administrative commands.
- **FR-6.2**: CLI tool `processpilot` must implement commands: `status`, `start`, `stop`, `restart`, `monitor`, `graph`, `kill`, `logs`, `reload`.

---

## 2. Non-Functional Requirements (NFR)

- **NFR-1 (Portability)**: Target Linux OS kernels 3.10+ using POSIX system APIs.
- **NFR-2 (Performance & Efficiency)**: CPU overhead of the supervisor daemon must remain below 0.5% under idle conditions. Zero busy-waiting loops.
- **NFR-3 (Robustness & Signal Safety)**: All asynchronous signal processing must use async-signal-safe primitives (Self-Pipe trick).
- **NFR-4 (Modern C++)**: Compliant with ISO C++17 standard with strict compiler warnings enabled (`-Wall -Wextra -Wpedantic`).
