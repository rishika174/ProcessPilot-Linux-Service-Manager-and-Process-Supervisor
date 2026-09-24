#pragma once

#include "config/config_parser.hpp"
#include "dependency/dependency_graph.hpp"
#include "process/process_manager.hpp"
#include "ipc/unix_socket.hpp"
#include <string>
#include <vector>
#include <atomic>
#include <memory>

namespace processpilot {

class Daemon {
public:
    Daemon(const std::string& configDir = "/etc/processpilot/services.d",
           const std::string& socketPath = "/tmp/processpilot.sock",
           const std::string& logFilePath = "/tmp/processpilot.log");
    ~Daemon();

    bool initialize();
    int run();
    void stop();

    // Signal handlers
    static void setupSignalHandlers();
    static void signalHandler(int sig);

private:
    std::string configDir_;
    std::string socketPath_;
    std::string logFilePath_;

    std::atomic<bool> isRunning_{false};
    static std::atomic<bool> terminationRequested_;
    static std::atomic<bool> reloadRequested_;
    static std::atomic<bool> childSignaled_;
    static int signalPipe_[2];

    ProcessManager processManager_;
    DependencyGraph dependencyGraph_;
    std::unique_ptr<UnixSocketServer> ipcServer_;

    void loadConfigurations();
    void startAutoServices();
    void gracefulShutdownAll();

    // Command dispatching
    std::string handleIpcCommand(const std::string& request);
    std::string formatStatusTable(const std::string& specificService = "");
    std::string formatMonitorTable();
    std::string formatDependencyTree();
};

} // namespace processpilot
