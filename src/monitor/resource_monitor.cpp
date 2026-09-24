#include "monitor/resource_monitor.hpp"
#include "logging/logger.hpp"
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <iomanip>
#include <cmath>

namespace processpilot {

ResourceMonitor::ResourceMonitor() {
    clockTicksPerSecond_ = sysconf(_SC_CLK_TCK);
    if (clockTicksPerSecond_ <= 0) clockTicksPerSecond_ = 100;

    numProcessors_ = sysconf(_SC_NPROCESSORS_ONLN);
    if (numProcessors_ <= 0) numProcessors_ = 1;

    pageSizeBytes_ = sysconf(_SC_PAGESIZE);
    if (pageSizeBytes_ <= 0) pageSizeBytes_ = 4096;
}

uint64_t ResourceMonitor::readTotalSystemJiffies() {
    std::ifstream statFile("/proc/stat");
    if (!statFile.is_open()) return 0;

    std::string line;
    if (std::getline(statFile, line)) {
        if (line.rfind("cpu ", 0) == 0) {
            std::istringstream ss(line.substr(4));
            uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
            ss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
            return user + nice + system + idle + iowait + irq + softirq + steal;
        }
    }
    return 0;
}

ProcessMetrics ResourceMonitor::sampleProcess(pid_t pid) {
    ProcessMetrics metrics;
    metrics.pid = pid;

    if (pid <= 0) {
        return metrics;
    }

    // Read /proc/[pid]/stat
    std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream statFile(statPath);
    if (!statFile.is_open()) {
        // Process no longer exists or permission denied
        lastSamples_.erase(pid);
        return metrics;
    }

    std::string line;
    if (!std::getline(statFile, line)) {
        lastSamples_.erase(pid);
        return metrics;
    }

    // Linux /proc/[pid]/stat format:
    // pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime ...
    auto openParen = line.find('(');
    auto closeParen = line.rfind(')');
    if (openParen == std::string::npos || closeParen == std::string::npos || closeParen <= openParen) {
        return metrics;
    }

    metrics.isAlive = true;
    std::string rest = line.substr(closeParen + 2); // skip ") "
    std::istringstream ss(rest);

    char state = '?';
    int ppid = 0, pgrp = 0, session = 0, tty_nr = 0, tpgid = 0;
    unsigned long flags = 0, minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0;
    unsigned long utime = 0, stime = 0;
    long cutime = 0, cstime = 0, priority = 0, niceVal = 0, numThreads = 0, itrealvalue = 0;
    unsigned long long starttime = 0;
    unsigned long vsize = 0;
    long rssPages = 0;

    ss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid
       >> flags >> minflt >> cminflt >> majflt >> cmajflt
       >> utime >> stime >> cutime >> cstime >> priority >> niceVal
       >> numThreads >> itrealvalue >> starttime >> vsize >> rssPages;

    metrics.state = state;
    metrics.numThreads = numThreads;
    metrics.vmsBytes = vsize;
    metrics.rssBytes = static_cast<uint64_t>(std::max(0L, rssPages)) * pageSizeBytes_;

    // Calculate Uptime
    std::ifstream uptimeFile("/proc/uptime");
    if (uptimeFile.is_open()) {
        double sysUptime = 0.0;
        uptimeFile >> sysUptime;
        double procStartSeconds = static_cast<double>(starttime) / clockTicksPerSecond_;
        if (sysUptime > procStartSeconds) {
            metrics.uptimeSeconds = static_cast<uint64_t>(sysUptime - procStartSeconds);
        }
    }

    // Calculate CPU usage using delta
    auto now = std::chrono::steady_clock::now();
    uint64_t processJiffies = utime + stime;
    uint64_t systemJiffies = readTotalSystemJiffies();

    auto it = lastSamples_.find(pid);
    if (it != lastSamples_.end()) {
        uint64_t deltaProcess = (processJiffies >= it->second.processJiffies)
                                    ? (processJiffies - it->second.processJiffies)
                                    : 0;
        uint64_t deltaSystem = (systemJiffies >= it->second.systemJiffies)
                                   ? (systemJiffies - it->second.systemJiffies)
                                   : 0;

        auto timeDeltaMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.timestamp).count();

        if (deltaSystem > 0) {
            // Standard system CPU fraction
            metrics.cpuPercentage = (static_cast<double>(deltaProcess) / static_cast<double>(deltaSystem)) * 100.0 * numProcessors_;
        } else if (timeDeltaMs > 0) {
            // Fallback using wall-clock time
            double seconds = timeDeltaMs / 1000.0;
            metrics.cpuPercentage = ((static_cast<double>(deltaProcess) / clockTicksPerSecond_) / seconds) * 100.0;
        }

        if (metrics.cpuPercentage < 0.0) metrics.cpuPercentage = 0.0;
        if (metrics.cpuPercentage > 100.0 * numProcessors_) metrics.cpuPercentage = 100.0 * numProcessors_;
    } else {
        metrics.cpuPercentage = 0.0;
    }

    lastSamples_[pid] = CpuSample{processJiffies, systemJiffies, now};
    return metrics;
}

SystemMetrics ResourceMonitor::sampleSystem() {
    SystemMetrics sys;

    // Read /proc/meminfo
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo.is_open()) {
        std::string line;
        uint64_t memTotalKb = 0, memAvailableKb = 0, memFreeKb = 0;
        while (std::getline(meminfo, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                std::istringstream ss(line.substr(9));
                ss >> memTotalKb;
            } else if (line.rfind("MemAvailable:", 0) == 0) {
                std::istringstream ss(line.substr(13));
                ss >> memAvailableKb;
            } else if (line.rfind("MemFree:", 0) == 0) {
                std::istringstream ss(line.substr(8));
                ss >> memFreeKb;
            }
        }
        sys.totalMemBytes = memTotalKb * 1024;
        if (memAvailableKb > 0) {
            sys.freeMemBytes = memAvailableKb * 1024;
            sys.usedMemBytes = sys.totalMemBytes - sys.freeMemBytes;
        } else {
            sys.freeMemBytes = memFreeKb * 1024;
            sys.usedMemBytes = sys.totalMemBytes - sys.freeMemBytes;
        }
    }

    return sys;
}

void ResourceMonitor::removeProcess(pid_t pid) {
    lastSamples_.erase(pid);
}

std::string ResourceMonitor::formatBytes(uint64_t bytes) {
    const char* suffixes[] = {"B", "KB", "MB", "GB", "TB"};
    int s = 0;
    double count = static_cast<double>(bytes);
    while (count >= 1024 && s < 4) {
        s++;
        count /= 1024;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << count << " " << suffixes[s];
    return ss.str();
}

} // namespace processpilot
