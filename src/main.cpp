#include "daemon/daemon.hpp"
#include <iostream>
#include <string>

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]\n"
              << "Options:\n"
              << "  -c, --config-dir <path>   Path to service configuration directory (default: ./configs)\n"
              << "  -s, --socket <path>       UNIX socket path (default: /tmp/processpilot.sock)\n"
              << "  -l, --log <path>          Log file path (default: /tmp/processpilot.log)\n"
              << "  -h, --help                Display this help message\n";
}

int main(int argc, char* argv[]) {
    std::string configDir = "./configs";
    std::string socketPath = "/tmp/processpilot.sock";
    std::string logPath = "/tmp/processpilot.log";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config-dir") && i + 1 < argc) {
            configDir = argv[++i];
        } else if ((arg == "-s" || arg == "--socket") && i + 1 < argc) {
            socketPath = argv[++i];
        } else if ((arg == "-l" || arg == "--log") && i + 1 < argc) {
            logPath = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    std::cout << "========================================================\n"
              << "  ProcessPilot - Linux Service Supervisor Daemon v1.0   \n"
              << "========================================================\n"
              << "Config Directory: " << configDir << "\n"
              << "IPC Socket:       " << socketPath << "\n"
              << "Log File:         " << logPath << "\n\n";

    processpilot::Daemon daemon(configDir, socketPath, logPath);
    if (!daemon.initialize()) {
        std::cerr << "Failed to initialize ProcessPilot daemon\n";
        return 1;
    }

    return daemon.run();
}
