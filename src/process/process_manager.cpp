#include "process/process_manager.hpp"
#include "logging/logger.hpp"
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <algorithm>

namespace processpilot {

std::string ProcessInfo::stateToString(ProcessState state) {
    switch (state) {
        case ProcessState::STOPPED:    return "STOPPED";
        case ProcessState::STARTING:   return "STARTING";
        case ProcessState::RUNNING:    return "RUNNING";
        case ProcessState::CRASHED:    return "CRASHED";
        case ProcessState::RESTARTING: return "RESTARTING";
        case ProcessState::DEAD:       return "DEAD (FLAPPING)";
        default:                       return "UNKNOWN";
    }
}

ProcessManager::ProcessManager() = default;

ProcessManager::~ProcessManager() {
    std::lock_guard<std::mutex> lock(managerMutex_);
    for (auto& [name, info] : services_) {
        if (info.pid > 0 && info.state == ProcessState::RUNNING) {
            kill(info.pid, SIGTERM);
        }
        if (info.stdoutPipeFd >= 0) close(info.stdoutPipeFd);
        if (info.stderrPipeFd >= 0) close(info.stderrPipeFd);
    }
}

bool ProcessManager::registerService(const ServiceConfig& config) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    if (services_.find(config.name) != services_.end()) {
        services_[config.name].config = config;
        return true;
    }

    ProcessInfo info;
    info.name = config.name;
    info.config = config;
    info.state = ProcessState::STOPPED;
    services_[config.name] = std::move(info);
    LOG_INFO("ProcessManager", "Registered service: " + config.name);
    return true;
}

bool ProcessManager::unregisterService(const std::string& name) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    auto it = services_.find(name);
    if (it == services_.end()) return false;

    if (it->second.state == ProcessState::RUNNING) {
        LOG_WARN("ProcessManager", "Stopping service before unregistering: " + name);
        if (it->second.pid > 0) {
            kill(it->second.pid, SIGTERM);
        }
    }
    services_.erase(it);
    return true;
}

bool ProcessManager::spawnProcess(ProcessInfo& info) {
    int outPipe[2];
    int errPipe[2];

    if (pipe(outPipe) < 0 || pipe(errPipe) < 0) {
        LOG_ERROR("ProcessManager", "Failed to create IPC pipes for " + info.name);
        return false;
    }

    info.userRequestedStop = false;
    info.state = ProcessState::STARTING;

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("ProcessManager", "Fork failed for service " + info.name + ": " + strerror(errno));
        close(outPipe[0]); close(outPipe[1]);
        close(errPipe[0]); close(errPipe[1]);
        info.state = ProcessState::CRASHED;
        return false;
    }

    if (pid == 0) {
        // --- CHILD PROCESS ---
        close(outPipe[0]); // Close read end
        close(errPipe[0]); // Close read end

        // Redirect stdout & stderr to pipes
        dup2(outPipe[1], STDOUT_FILENO);
        dup2(errPipe[1], STDERR_FILENO);
        close(outPipe[1]);
        close(errPipe[1]);

        // Set working directory if configured
        if (!info.config.workingDirectory.empty()) {
            if (chdir(info.config.workingDirectory.c_str()) != 0) {
                std::cerr << "Failed to change directory to: " << info.config.workingDirectory << "\n";
            }
        }

        // Set environment variables
        for (const auto& [k, v] : info.config.environment) {
            setenv(k.c_str(), v.c_str(), 1);
        }

        // Execute service command via shell to support arguments and pipelines
        execl("/bin/sh", "sh", "-c", info.config.execStart.c_str(), (char*)NULL);

        // If exec fails
        std::cerr << "ProcessPilot exec failed: " << strerror(errno) << "\n";
        _exit(127);
    }

    // --- PARENT PROCESS ---
    close(outPipe[1]); // Close write end in parent
    close(errPipe[1]); // Close write end in parent

    // Set read ends to non-blocking
    fcntl(outPipe[0], F_SETFL, O_NONBLOCK);
    fcntl(errPipe[0], F_SETFL, O_NONBLOCK);

    if (info.stdoutPipeFd >= 0) close(info.stdoutPipeFd);
    if (info.stderrPipeFd >= 0) close(info.stderrPipeFd);

    info.stdoutPipeFd = outPipe[0];
    info.stderrPipeFd = errPipe[0];
    info.pid = pid;
    info.startTime = std::chrono::system_clock::now();
    info.state = ProcessState::RUNNING;

    pidToServiceName_[pid] = info.name;

    LOG_INFO("ProcessManager", "Started service [" + info.name + "] with PID " + std::to_string(pid));
    return true;
}

bool ProcessManager::startService(const std::string& name) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    auto it = services_.find(name);
    if (it == services_.end()) {
        LOG_ERROR("ProcessManager", "Service not found: " + name);
        return false;
    }

    if (it->second.state == ProcessState::RUNNING) {
        LOG_WARN("ProcessManager", "Service is already running: " + name);
        return true;
    }

    it->second.restartCount = 0; // Manual start resets crash counter
    it->second.crashHistory.clear();
    return spawnProcess(it->second);
}

bool ProcessManager::stopService(const std::string& name, int timeoutSec) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    auto it = services_.find(name);
    if (it == services_.end()) return false;

    if (it->second.state != ProcessState::RUNNING || it->second.pid <= 0) {
        it->second.state = ProcessState::STOPPED;
        return true;
    }

    it->second.userRequestedStop = true;
    pid_t pid = it->second.pid;
    LOG_INFO("ProcessManager", "Stopping service [" + name + "] (PID " + std::to_string(pid) + ")");

    // Execute ExecStop if provided, otherwise send SIGTERM
    if (!it->second.config.execStop.empty()) {
        system(it->second.config.execStop.c_str());
    } else {
        kill(pid, SIGTERM);
    }

    // Wait up to timeoutSec for graceful termination
    int elapsedMs = 0;
    int checkIntervalMs = 100;
    int maxWaitMs = timeoutSec * 1000;
    bool terminated = false;

    while (elapsedMs < maxWaitMs) {
        int status;
        pid_t res = waitpid(pid, &status, WNOHANG);
        if (res == pid || (res < 0 && errno == ECHILD)) {
            terminated = true;
            break;
        }
        usleep(checkIntervalMs * 1000);
        elapsedMs += checkIntervalMs;
    }

    if (!terminated) {
        LOG_WARN("ProcessManager", "Service [" + name + "] did not exit within " +
                 std::to_string(timeoutSec) + "s, escalating to SIGKILL");
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    }

    it->second.state = ProcessState::STOPPED;
    it->second.pid = 0;
    pidToServiceName_.erase(pid);
    monitor_.removeProcess(pid);

    LOG_INFO("ProcessManager", "Service [" + name + "] stopped successfully");
    return true;
}

bool ProcessManager::restartService(const std::string& name) {
    stopService(name, 3);
    return startService(name);
}

bool ProcessManager::sendSignal(const std::string& name, int signum) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    auto it = services_.find(name);
    if (it == services_.end() || it->second.pid <= 0) {
        return false;
    }

    LOG_INFO("ProcessManager", "Sending signal " + std::to_string(signum) + " to " + name + " (PID " + std::to_string(it->second.pid) + ")");
    return (kill(it->second.pid, signum) == 0);
}

bool ProcessManager::shouldRestart(const ProcessInfo& info, int waitStatus) {
    if (info.userRequestedStop) {
        return false; // Manually stopped by user
    }

    bool isCrash = false;
    if (WIFEXITED(waitStatus)) {
        int exitCode = WEXITSTATUS(waitStatus);
        if (exitCode != 0) isCrash = true;
    } else if (WIFSIGNALED(waitStatus)) {
        isCrash = true;
    }

    switch (info.config.restartPolicy) {
        case RestartPolicy::ALWAYS:
            return true;
        case RestartPolicy::ON_FAILURE:
            return isCrash;
        case RestartPolicy::ON_ABORT:
            return WIFSIGNALED(waitStatus);
        case RestartPolicy::NO:
        default:
            return false;
    }
}

void ProcessManager::handleChildExit(ProcessInfo& info, int waitStatus) {
    pid_t deadPid = info.pid;
    info.pid = 0;
    info.lastExitTime = std::chrono::system_clock::now();
    pidToServiceName_.erase(deadPid);
    monitor_.removeProcess(deadPid);

    if (WIFEXITED(waitStatus)) {
        info.lastExitCode = WEXITSTATUS(waitStatus);
        info.lastTermSignal = 0;
        LOG_INFO("ProcessManager", "Process [" + info.name + "] exited with code " + std::to_string(info.lastExitCode));
    } else if (WIFSIGNALED(waitStatus)) {
        info.lastExitCode = -1;
        info.lastTermSignal = WTERMSIG(waitStatus);
        LOG_WARN("ProcessManager", "Process [" + info.name + "] terminated by signal " +
                 std::to_string(info.lastTermSignal) + " (" + strsignal(info.lastTermSignal) + ")");
    }

    // Check restart policy
    if (shouldRestart(info, waitStatus)) {
        auto now = std::chrono::system_clock::now();
        info.crashHistory.push_back(now);

        // Prune crash history outside window
        auto windowStart = now - std::chrono::seconds(info.config.restartWindowSec);
        info.crashHistory.erase(
            std::remove_if(info.crashHistory.begin(), info.crashHistory.end(),
                           [&](const auto& t) { return t < windowStart; }),
            info.crashHistory.end()
        );

        // Check if flapping limit exceeded
        if (static_cast<int>(info.crashHistory.size()) > info.config.maxRestarts) {
            info.state = ProcessState::DEAD;
            LOG_CRIT("ProcessManager", "Service [" + info.name + "] flapping! Exceeded " +
                     std::to_string(info.config.maxRestarts) + " restarts within " +
                     std::to_string(info.config.restartWindowSec) + "s. Marked as DEAD.");
            return;
        }

        info.restartCount++;
        info.state = ProcessState::RESTARTING;
        info.nextRestartTime = std::chrono::steady_clock::now() + std::chrono::seconds(info.config.restartSec);

        LOG_WARN("ProcessManager", "Service [" + info.name + "] crashed! Auto-restart scheduled in " +
                 std::to_string(info.config.restartSec) + "s (Attempt #" + std::to_string(info.restartCount) + ")");
    } else {
        info.state = ProcessState::STOPPED;
    }
}

void ProcessManager::reapChildren() {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        std::lock_guard<std::mutex> lock(managerMutex_);
        auto it = pidToServiceName_.find(pid);
        if (it != pidToServiceName_.end()) {
            std::string serviceName = it->second;
            auto sIt = services_.find(serviceName);
            if (sIt != services_.end()) {
                handleChildExit(sIt->second, status);
            }
        }
    }
}

void ProcessManager::processRestartQueue() {
    std::lock_guard<std::mutex> lock(managerMutex_);
    auto now = std::chrono::steady_clock::now();

    for (auto& [name, info] : services_) {
        if (info.state == ProcessState::RESTARTING && now >= info.nextRestartTime) {
            LOG_INFO("ProcessManager", "Executing auto-restart for service: " + name);
            spawnProcess(info);
        }
    }
}

void ProcessManager::updateMetrics() {
    std::lock_guard<std::mutex> lock(managerMutex_);
    for (auto& [name, info] : services_) {
        if (info.state == ProcessState::RUNNING && info.pid > 0) {
            info.metrics = monitor_.sampleProcess(info.pid);
        } else {
            info.metrics.cpuPercentage = 0.0;
            info.metrics.rssBytes = 0;
            info.metrics.vmsBytes = 0;
            info.metrics.isAlive = false;
        }
    }
}

void ProcessManager::readProcessOutput() {
    std::lock_guard<std::mutex> lock(managerMutex_);
    char buf[1024];

    for (auto& [name, info] : services_) {
        // Read stdout
        if (info.stdoutPipeFd >= 0) {
            ssize_t bytesRead = 0;
            while ((bytesRead = read(info.stdoutPipeFd, buf, sizeof(buf) - 1)) > 0) {
                buf[bytesRead] = '\0';
                std::string s(buf);
                std::stringstream ss(s);
                std::string line;
                while (std::getline(ss, line)) {
                    if (!line.empty()) {
                        info.logRingBuffer.push_back("[stdout] " + line);
                        if (info.logRingBuffer.size() > 500) info.logRingBuffer.pop_front();
                    }
                }
            }
        }
        // Read stderr
        if (info.stderrPipeFd >= 0) {
            ssize_t bytesRead = 0;
            while ((bytesRead = read(info.stderrPipeFd, buf, sizeof(buf) - 1)) > 0) {
                buf[bytesRead] = '\0';
                std::string s(buf);
                std::stringstream ss(s);
                std::string line;
                while (std::getline(ss, line)) {
                    if (!line.empty()) {
                        info.logRingBuffer.push_back("[stderr] " + line);
                        if (info.logRingBuffer.size() > 500) info.logRingBuffer.pop_front();
                    }
                }
            }
        }
    }
}

std::optional<ProcessInfo> ProcessManager::getServiceInfo(const std::string& name) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    auto it = services_.find(name);
    if (it != services_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<ProcessInfo> ProcessManager::getAllServices() {
    std::lock_guard<std::mutex> lock(managerMutex_);
    std::vector<ProcessInfo> result;
    for (const auto& [_, info] : services_) {
        result.push_back(info);
    }
    return result;
}

std::vector<std::string> ProcessManager::getLogs(const std::string& name, size_t maxLines) {
    std::lock_guard<std::mutex> lock(managerMutex_);
    auto it = services_.find(name);
    if (it == services_.end()) return {};

    std::vector<std::string> lines;
    size_t count = std::min(maxLines, it->second.logRingBuffer.size());
    auto startIt = it->second.logRingBuffer.end() - count;
    for (auto i = startIt; i != it->second.logRingBuffer.end(); ++i) {
        lines.push_back(*i);
    }
    return lines;
}

} // namespace processpilot
