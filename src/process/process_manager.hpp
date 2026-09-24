#pragma once

#include "config/config_parser.hpp"
#include "monitor/resource_monitor.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>
#include <chrono>
#include <mutex>
#include <memory>
#include <sys/types.h>

namespace processpilot {

enum class ProcessState {
    STOPPED,
    STARTING,
    RUNNING,
    CRASHED,
    RESTARTING,
    DEAD       // Exhausted max restarts
};

struct ProcessInfo {
    std::string name;
    ServiceConfig config;
    ProcessState state{ProcessState::STOPPED};
    pid_t pid{0};
    
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point lastExitTime;
    std::chrono::steady_clock::time_point nextRestartTime;
    
    int lastExitCode{0};
    int lastTermSignal{0};
    int restartCount{0};
    std::vector<std::chrono::system_clock::time_point> crashHistory;

    ProcessMetrics metrics;
    std::deque<std::string> logRingBuffer; // Recent stdout/stderr lines
    int stdoutPipeFd{-1};
    int stderrPipeFd{-1};

    bool userRequestedStop{false};

    static std::string stateToString(ProcessState state);
};

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    bool registerService(const ServiceConfig& config);
    bool unregisterService(const std::string& name);

    bool startService(const std::string& name);
    bool stopService(const std::string& name, int timeoutSec = 5);
    bool restartService(const std::string& name);

    // Reap child processes using waitpid(WNOHANG) and trigger crash handling
    void reapChildren();

    // Check services pending restart backoff timer
    void processRestartQueue();

    // Update resource metrics for all running processes
    void updateMetrics();

    // Send arbitrary signal to a service process
    bool sendSignal(const std::string& name, int signum);

    // Query status
    std::optional<ProcessInfo> getServiceInfo(const std::string& name);
    std::vector<ProcessInfo> getAllServices();

    // Get logs for service
    std::vector<std::string> getLogs(const std::string& name, size_t maxLines = 50);

    // Drain pipes for running processes (non-blocking stdout/stderr capture)
    void readProcessOutput();

private:
    std::mutex managerMutex_;
    std::unordered_map<std::string, ProcessInfo> services_;
    std::unordered_map<pid_t, std::string> pidToServiceName_;
    ResourceMonitor monitor_;

    bool spawnProcess(ProcessInfo& info);
    void handleChildExit(ProcessInfo& info, int waitStatus);
    bool shouldRestart(const ProcessInfo& info, int waitStatus);
};

} // namespace processpilot
