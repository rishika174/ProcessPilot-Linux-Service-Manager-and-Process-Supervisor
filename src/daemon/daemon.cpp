#include "daemon/daemon.hpp"
#include "logging/logger.hpp"
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace processpilot {

std::atomic<bool> Daemon::terminationRequested_{false};
std::atomic<bool> Daemon::reloadRequested_{false};
std::atomic<bool> Daemon::childSignaled_{false};
int Daemon::signalPipe_[2] = {-1, -1};

void Daemon::signalHandler(int sig) {
    char ch = static_cast<char>(sig);
    if (signalPipe_[1] >= 0) {
        // Asynchronously signal-safe write to self-pipe
        ssize_t ret = write(signalPipe_[1], &ch, 1);
        (void)ret;
    }

    if (sig == SIGTERM || sig == SIGINT) {
        terminationRequested_ = true;
    } else if (sig == SIGHUP) {
        reloadRequested_ = true;
    } else if (sig == SIGCHLD) {
        childSignaled_ = true;
    }
}

void Daemon::setupSignalHandlers() {
    if (pipe(signalPipe_) < 0) {
        LOG_ERROR("Daemon", "Failed to create signal self-pipe");
        return;
    }
    // Set non-blocking on pipe ends
    fcntl(signalPipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(signalPipe_[1], F_SETFL, O_NONBLOCK);

    struct sigaction sa{};
    sa.sa_handler = Daemon::signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGHUP,  &sa, nullptr);
    sigaction(SIGCHLD, &sa, nullptr);

    // Ignore SIGPIPE to avoid exiting when socket disconnects abruptly
    signal(SIGPIPE, SIG_IGN);
}

Daemon::Daemon(const std::string& configDir,
               const std::string& socketPath,
               const std::string& logFilePath)
    : configDir_(configDir), socketPath_(socketPath), logFilePath_(logFilePath) {}

Daemon::~Daemon() {
    stop();
    if (signalPipe_[0] >= 0) close(signalPipe_[0]);
    if (signalPipe_[1] >= 0) close(signalPipe_[1]);
}

bool Daemon::initialize() {
    Logger::getInstance().init(logFilePath_, LogLevel::INFO, true);
    LOG_INFO("Daemon", "Initializing ProcessPilot Supervisor Daemon v1.0.0");

    setupSignalHandlers();

    loadConfigurations();

    ipcServer_ = std::make_unique<UnixSocketServer>(socketPath_);
    if (!ipcServer_->start([this](const std::string& req) {
        return handleIpcCommand(req);
    })) {
        LOG_ERROR("Daemon", "Failed to start IPC server at " + socketPath_);
        return false;
    }

    LOG_INFO("Daemon", "ProcessPilot supervisor initialized successfully");
    return true;
}

void Daemon::loadConfigurations() {
    LOG_INFO("Daemon", "Scanning configuration directory: " + configDir_);
    auto configs = ConfigParser::parseDirectory(configDir_);

    for (const auto& cfg : configs) {
        processManager_.registerService(cfg);
    }

    dependencyGraph_.buildGraph(configs);

    std::vector<std::string> cycle;
    if (dependencyGraph_.hasCycles(cycle)) {
        std::string cycleStr;
        for (const auto& s : cycle) cycleStr += s + " -> ";
        LOG_CRIT("Daemon", "CYCLIC DEPENDENCY DETECTED IN SERVICES: " + cycleStr);
    } else {
        auto order = dependencyGraph_.getStartupOrder();
        std::string orderStr;
        for (const auto& s : order) orderStr += s + " ";
        LOG_INFO("Daemon", "Calculated topological startup order: [ " + orderStr + "]");
    }
}

void Daemon::startAutoServices() {
    auto startupOrder = dependencyGraph_.getStartupOrder();
    LOG_INFO("Daemon", "Starting enabled services in dependency order...");

    for (const auto& name : startupOrder) {
        auto info = processManager_.getServiceInfo(name);
        if (info && info->config.autoStart) {
            // Check if dependencies are running
            auto deps = dependencyGraph_.getDependencies(name);
            bool allDepsRunning = true;
            for (const auto& dep : deps) {
                auto depInfo = processManager_.getServiceInfo(dep);
                if (!depInfo || depInfo->state != ProcessState::RUNNING) {
                    LOG_WARN("Daemon", "Cannot start " + name + ": prerequisite " + dep + " is not running!");
                    allDepsRunning = false;
                    break;
                }
            }

            if (allDepsRunning) {
                processManager_.startService(name);
            }
        }
    }
}

void Daemon::gracefulShutdownAll() {
    LOG_INFO("Daemon", "Initiating graceful shutdown of all services...");
    auto shutdownOrder = dependencyGraph_.getShutdownOrder();

    for (const auto& name : shutdownOrder) {
        auto info = processManager_.getServiceInfo(name);
        if (info && info->state == ProcessState::RUNNING) {
            processManager_.stopService(name, 5);
        }
    }
    LOG_INFO("Daemon", "All services shut down gracefully");
}

int Daemon::run() {
    isRunning_ = true;
    startAutoServices();

    auto lastMetricsUpdate = std::chrono::steady_clock::now();

    while (isRunning_ && !terminationRequested_) {
        // Sleep / wait for signal or timeout (50ms)
        fd_set readFds;
        FD_ZERO(&readFds);
        int maxFd = -1;

        if (signalPipe_[0] >= 0) {
            FD_SET(signalPipe_[0], &readFds);
            maxFd = signalPipe_[0];
        }

        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 50000; // 50ms tick

        int ret = select(maxFd + 1, &readFds, nullptr, nullptr, &tv);

        // Check self-pipe tokens
        if (ret > 0 && FD_ISSET(signalPipe_[0], &readFds)) {
            char buf[64];
            while (read(signalPipe_[0], buf, sizeof(buf)) > 0) {}
        }

        // 1. Always reap children (handles crashes and exits instantly)
        processManager_.reapChildren();

        // 2. Read non-blocking stdout/stderr pipes from active services
        processManager_.readProcessOutput();

        // 3. Process services in backoff queue ready to restart
        processManager_.processRestartQueue();

        // 4. Periodically update resource metrics (every 1 second)
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMetricsUpdate).count() >= 1000) {
            processManager_.updateMetrics();
            lastMetricsUpdate = now;
        }

        // 5. Handle reload request (SIGHUP)
        if (reloadRequested_) {
            reloadRequested_ = false;
            LOG_INFO("Daemon", "SIGHUP received, reloading configurations...");
            loadConfigurations();
        }
    }

    gracefulShutdownAll();
    stop();
    return 0;
}

void Daemon::stop() {
    isRunning_ = false;
    if (ipcServer_) {
        ipcServer_->stop();
    }
}

// ==================== IPC Command Processing ====================

std::string Daemon::handleIpcCommand(const std::string& request) {
    std::istringstream ss(request);
    std::string cmd;
    ss >> cmd;

    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if (cmd == "STATUS") {
        std::string target;
        ss >> target;
        return formatStatusTable(target);
    } else if (cmd == "START") {
        std::string target;
        ss >> target;
        if (target.empty()) return "ERROR: Missing service name. Usage: start <service>";

        // Verify dependencies
        auto deps = dependencyGraph_.getDependencies(target);
        for (const auto& dep : deps) {
            auto dInfo = processManager_.getServiceInfo(dep);
            if (!dInfo || dInfo->state != ProcessState::RUNNING) {
                // Auto-start dependency if requested
                LOG_INFO("Daemon", "Auto-starting dependency [" + dep + "] before [" + target + "]");
                processManager_.startService(dep);
            }
        }

        if (processManager_.startService(target)) {
            return "SUCCESS: Service [" + target + "] starting";
        } else {
            return "ERROR: Failed to start service [" + target + "]";
        }
    } else if (cmd == "STOP") {
        std::string target;
        ss >> target;
        if (target.empty()) return "ERROR: Missing service name. Usage: stop <service>";

        // Check if other running services depend on this
        auto dependents = dependencyGraph_.getDependents(target);
        std::string warning;
        for (const auto& dep : dependents) {
            auto dInfo = processManager_.getServiceInfo(dep);
            if (dInfo && dInfo->state == ProcessState::RUNNING) {
                warning += "\nWARNING: Service [" + dep + "] depends on [" + target + "] and is still running!";
            }
        }

        if (processManager_.stopService(target)) {
            return "SUCCESS: Service [" + target + "] stopped" + warning;
        } else {
            return "ERROR: Failed to stop service [" + target + "]";
        }
    } else if (cmd == "RESTART") {
        std::string target;
        ss >> target;
        if (target.empty()) return "ERROR: Missing service name. Usage: restart <service>";

        if (processManager_.restartService(target)) {
            return "SUCCESS: Service [" + target + "] restarted";
        } else {
            return "ERROR: Failed to restart service [" + target + "]";
        }
    } else if (cmd == "KILL") {
        std::string target;
        int sig = SIGTERM;
        ss >> target >> sig;
        if (target.empty()) return "ERROR: Missing service name. Usage: kill <service> [signal]";
        if (sig <= 0) sig = SIGTERM;

        if (processManager_.sendSignal(target, sig)) {
            return "SUCCESS: Sent signal " + std::to_string(sig) + " to [" + target + "]";
        } else {
            return "ERROR: Failed to signal [" + target + "]";
        }
    } else if (cmd == "MONITOR") {
        return formatMonitorTable();
    } else if (cmd == "GRAPH") {
        return formatDependencyTree();
    } else if (cmd == "RELOAD") {
        loadConfigurations();
        return "SUCCESS: Configurations reloaded";
    } else if (cmd == "LOGS") {
        std::string target;
        size_t lines = 30;
        ss >> target >> lines;
        if (target.empty()) return "ERROR: Missing service name. Usage: logs <service> [num_lines]";

        auto logLines = processManager_.getLogs(target, lines);
        std::ostringstream oss;
        oss << "=== Logs for " << target << " (" << logLines.size() << " lines) ===\n";
        for (const auto& l : logLines) oss << l << "\n";
        return oss.str();
    } else if (cmd == "HELP") {
        return "ProcessPilot Commands:\n"
               "  status [service]      - Show current status of services\n"
               "  start <service>       - Start a service (resolving dependencies)\n"
               "  stop <service>        - Stop a running service\n"
               "  restart <service>     - Restart a service\n"
               "  kill <service> [sig]  - Send signal (e.g., 9 for SIGKILL, 11 for SIGSEGV)\n"
               "  monitor               - Live CPU and Memory consumption statistics\n"
               "  graph                 - Dependency tree hierarchy\n"
               "  logs <service> [n]    - Retrieve recent output logs\n"
               "  reload                - Reload service configuration files\n";
    }

    return "ERROR: Unknown command '" + cmd + "'. Type 'help' for available commands.";
}

std::string Daemon::formatStatusTable(const std::string& specificService) {
    std::ostringstream ss;
    auto services = processManager_.getAllServices();

    if (!specificService.empty()) {
        auto it = std::find_if(services.begin(), services.end(),
                               [&](const ProcessInfo& p) { return p.name == specificService; });
        if (it == services.end()) {
            return "ERROR: Service '" + specificService + "' not found.";
        }
        services = {*it};
    }

    ss << std::left
       << std::setw(18) << "SERVICE"
       << std::setw(16) << "STATE"
       << std::setw(10) << "PID"
       << std::setw(12) << "RESTARTS"
       << std::setw(14) << "POLICY"
       << std::setw(14) << "UPTIME"
       << "COMMAND\n";
    ss << std::string(88, '-') << "\n";

    for (const auto& svc : services) {
        std::string uptimeStr = "-";
        if (svc.state == ProcessState::RUNNING && svc.metrics.uptimeSeconds > 0) {
            uint64_t u = svc.metrics.uptimeSeconds;
            if (u >= 3600) {
                uptimeStr = std::to_string(u / 3600) + "h " + std::to_string((u % 3600) / 60) + "m";
            } else if (u >= 60) {
                uptimeStr = std::to_string(u / 60) + "m " + std::to_string(u % 60) + "s";
            } else {
                uptimeStr = std::to_string(u) + "s";
            }
        }

        ss << std::left
           << std::setw(18) << svc.name
           << std::setw(16) << ProcessInfo::stateToString(svc.state)
           << std::setw(10) << (svc.pid > 0 ? std::to_string(svc.pid) : "-")
           << std::setw(12) << svc.restartCount
           << std::setw(14) << ServiceConfig::restartPolicyToString(svc.config.restartPolicy)
           << std::setw(14) << uptimeStr
           << svc.config.execStart.substr(0, 30) << "\n";
    }

    return ss.str();
}

std::string Daemon::formatMonitorTable() {
    std::ostringstream ss;
    auto services = processManager_.getAllServices();

    ss << std::left
       << std::setw(18) << "SERVICE"
       << std::setw(10) << "PID"
       << std::setw(12) << "CPU %"
       << std::setw(14) << "RSS MEM"
       << std::setw(14) << "VM SIZE"
       << std::setw(10) << "THREADS"
       << "STATE\n";
    ss << std::string(82, '=') << "\n";

    for (const auto& svc : services) {
        if (svc.state == ProcessState::RUNNING) {
            std::ostringstream cpuSs;
            cpuSs << std::fixed << std::setprecision(1) << svc.metrics.cpuPercentage << "%";

            ss << std::left
               << std::setw(18) << svc.name
               << std::setw(10) << svc.pid
               << std::setw(12) << cpuSs.str()
               << std::setw(14) << ResourceMonitor::formatBytes(svc.metrics.rssBytes)
               << std::setw(14) << ResourceMonitor::formatBytes(svc.metrics.vmsBytes)
               << std::setw(10) << svc.metrics.numThreads
               << svc.metrics.state << "\n";
        }
    }

    return ss.str();
}

std::string Daemon::formatDependencyTree() {
    return dependencyGraph_.toAsciiTree();
}

} // namespace processpilot
