# ProcessPilot Architecture & System Design Document

## 1. System Overview
**ProcessPilot** is a lightweight, robust Linux service management and process supervision daemon written in modern C++ (C++17/20). It provides automated lifecycle supervision, dependency-ordered orchestration, real-time Linux resource monitoring, crash recovery with anti-flapping backoff, and local UNIX-domain socket inter-process communication.

```
+--------------------------------------------------------------------------+
|                            ProcessPilot CLI                              |
|         (processpilot status | start | stop | restart | monitor)         |
+------------------------------------+-------------------------------------+
                                     | UNIX Domain Socket (AF_UNIX)
                                     v
+------------------------------------+-------------------------------------+
|                      ProcessPilot Daemon (processpilotd)                 |
|                                                                          |
|  +-------------------+  +---------------------+  +--------------------+  |
|  |   Config Parser   |  |   Dependency DAG    |  |  Resource Monitor  |  |
|  | (systemd INI fmt) |  | (Topological Sort)  |  |   (/proc/pid/stat) |  |
|  +-------------------+  +---------------------+  +--------------------+  |
|                                                                          |
|  +--------------------------------------------------------------------+  |
|  |                        Process Manager                             |  |
|  |  - Fork & Exec Spawning         - Non-blocking Stdout/Stderr Pipes |  |
|  |  - waitpid(WNOHANG) Reaping     - Auto-Restart & Backoff Engine    |  |
|  |  - Flapping / Crash Protection  - Graceful SIGTERM Escalation      |  |
|  +--------------------------------------------------------------------+  |
|                                                                          |
|  +--------------------------------------------------------------------+  |
|  |                    Signal Dispatcher & Event Loop                  |  |
|  |  - Async Self-Pipe Trick        - SIGCHLD, SIGTERM, SIGHUP, SIGINT |  |
|  +--------------------------------------------------------------------+  |
+------------------------------------+-------------------------------------+
                                     | fork() / execvp() / pipes / signals
        +----------------------------+----------------------------+
        v                                                         v
 [Service: Database]                                     [Service: Web-API]
```

---

## 2. Process Lifecycle & State Machine

Every registered service is governed by a strict deterministic state machine:

```
      +-------------+
      |   STOPPED   | <-------------------------+
      +------+------+                           |
             | user start / daemon boot         | user stop (SIGTERM)
             v                                  |
      +-------------+                           |
      |  STARTING   |                           |
      +------+------+                           |
             | execvp() succeeds                |
             v                                  |
      +-------------+                           |
      |   RUNNING   | --------------------------+
      +------+------+
             | unexpected exit (code != 0 or signal)
             v
      +-------------+
      |   CRASHED   |
      +------+------+
             | policy allows restart (below MaxRestarts threshold)
             +------------------------------+
             |                              | exceeds MaxRestarts in window
             v                              v
      +-------------+                +-------------+
      | RESTARTING  |                |    DEAD     | (Flapping Protection)
      +------+------+                +-------------+
             | RestartSec backoff expires
             v
      (re-spawns -> STARTING -> RUNNING)
```

### State Definitions:
- **STOPPED**: Process is not running; PID is nullified.
- **STARTING**: Process has forked; environment and pipes are attached.
- **RUNNING**: Service process is alive and monitored via `/proc`.
- **CRASHED**: Process terminated unexpectedly due to non-zero exit code or terminating signal (`SIGSEGV`, `SIGKILL`, etc.).
- **RESTARTING**: Service is queued for automatic restart; waiting for backoff timer (`RestartSec`).
- **DEAD**: Service exceeded `MaxRestarts` within `RestartWindowSec`. Stopped permanently to prevent CPU thrashing.

---

## 3. Signal Handling & The Self-Pipe Pattern
Standard signal handlers cannot safely invoke complex memory operations or file I/O due to async-signal safety constraints.

ProcessPilot solves this using the **Self-Pipe Trick**:
1. An anonymous POSIX pipe `signalPipe_[2]` is created during initialization and set to non-blocking (`O_NONBLOCK`).
2. Asynchronous signal handlers (`SIGCHLD`, `SIGTERM`, `SIGINT`, `SIGHUP`) write a single byte token into `signalPipe_[1]`.
3. The main event loop monitors `signalPipe_[0]` via `select()` or non-blocking read.
4. When `SIGCHLD` arrives, the supervisor wakes up immediately and reaps dead children using `waitpid(-1, &status, WNOHANG)`.

---

## 4. Crash Detection & Exit Status Inspection
When a child terminates, the daemon inspects the raw wait status integer using POSIX macros:
- `WIFEXITED(status)`: Normal termination.
  - `WEXITSTATUS(status) == 0`: Clean exit.
  - `WEXITSTATUS(status) != 0`: Application error exit (triggers crash restart policy).
- `WIFSIGNALED(status)`: Terminated by uncaught POSIX signal.
  - `WTERMSIG(status)`: Signal identifier (e.g. `11` for `SIGSEGV`, `6` for `SIGABRT`).

---

## 5. Linux Resource Monitoring (/proc File System)
ProcessPilot reads Linux kernel system statistics directly from `/proc`:
1. **/proc/[pid]/stat**:
   - Parses `utime` (CPU ticks in user mode) and `stime` (CPU ticks in kernel mode).
   - CPU % calculation:
     $$\Delta \text{Ticks} = (\text{utime}_t + \text{stime}_t) - (\text{utime}_{t-1} + \text{stime}_{t-1})$$
     $$\text{CPU \%} = \frac{\Delta \text{Ticks}}{\Delta \text{System Jiffies}} \times 100 \times N_{\text{cores}}$$
2. **/proc/[pid]/status & stat**:
   - `rss` pages multiplied by `sysconf(_SC_PAGESIZE)` to obtain Resident Physical Memory.
   - `vmsize` to obtain Virtual Memory Size.
3. **/proc/meminfo**:
   - Reads `MemTotal` and `MemAvailable` for host-level memory pressure calculations.

---

## 6. Dependency Management (Directed Acyclic Graph)
Service startup and shutdown dependencies are structured as a Directed Acyclic Graph (DAG):
- **Cycle Detection**: Evaluated on boot using 3-color Depth First Search (White = unvisited, Gray = visiting, Black = visited). If a back-edge is encountered, the cycle path is logged and startup aborted.
- **Topological Sorting**: Evaluated using Kahn’s Algorithm:
  - In-degree of each node represents pending prerequisite services.
  - Nodes with in-degree 0 are ready to boot.
- **Graceful Shutdown**: The inverse topological order guarantees dependent applications shut down before their upstream databases or caches.

---

## 7. Inter-Process Communication (IPC)
- Domain: POSIX Local Stream Socket (`AF_UNIX`, `SOCK_STREAM`).
- Default path: `/tmp/processpilot.sock`.
- Framing: ASCII newline-delimited command request / formatted response.
- Supported operations: `status`, `start`, `stop`, `restart`, `monitor`, `graph`, `kill`, `logs`, `reload`.
