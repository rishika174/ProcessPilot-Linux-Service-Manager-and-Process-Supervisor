#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <sys/types.h>

namespace processpilot {

struct ProcessMetrics {
    pid_t pid{0};
    bool isAlive{false};
    char state{'?'};             // R (running), S (sleeping), Z (zombie), etc.
    double cpuPercentage{0.0};   // Instantaneous CPU %
    uint64_t rssBytes{0};        // Resident Set Size (physical memory) in bytes
    uint64_t vmsBytes{0};        // Virtual Memory Size in bytes
    int numThreads{0};
    uint64_t uptimeSeconds{0};
    uint64_t lastUpdateTime{0};
};

struct SystemMetrics {
    double totalCpuUsage{0.0};
    uint64_t totalMemBytes{0};
    uint64_t usedMemBytes{0};
    uint64_t freeMemBytes{0};
    int totalProcesses{0};
};

class ResourceMonitor {
public:
    ResourceMonitor();

    // Updates sample for given PID and returns computed metrics
    ProcessMetrics sampleProcess(pid_t pid);
    
    // System-wide resource metrics
    SystemMetrics sampleSystem();

    // Clear history when a process exits
    void removeProcess(pid_t pid);

    static std::string formatBytes(uint64_t bytes);

private:
    struct CpuSample {
        uint64_t processJiffies{0};
        uint64_t systemJiffies{0};
        std::chrono::steady_clock::time_point timestamp;
    };

    std::unordered_map<pid_t, CpuSample> lastSamples_;
    long clockTicksPerSecond_{100};
    int numProcessors_{1};
    long pageSizeBytes_{4096};

    uint64_t readTotalSystemJiffies();
};

} // namespace processpilot
