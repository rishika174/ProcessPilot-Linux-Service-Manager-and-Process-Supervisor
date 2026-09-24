#include "ipc/unix_socket.hpp"
#include <iostream>
#include <string>
#include <vector>

void printHelp() {
    std::cout << "\033[1;36mProcessPilot CLI - Linux Service Manager & Supervisor\033[0m\n\n"
              << "\033[1mUSAGE:\033[0m\n"
              << "  processpilot [options] <command> [arguments...]\n\n"
              << "\033[1mCOMMANDS:\033[0m\n"
              << "  \033[32mstatus\033[0m  [service]       Show runtime status of all services or specific service\n"
              << "  \033[32mstart\033[0m   <service>       Start service and resolve required dependencies\n"
              << "  \033[32mstop\033[0m    <service>       Gracefully stop service (escalates to SIGKILL if stuck)\n"
              << "  \033[32mrestart\033[0m <service>       Restart running service\n"
              << "  \033[32mmonitor\033[0m                 Display real-time CPU % and RSS memory utilization\n"
              << "  \033[32mgraph\033[0m                   Visualize service dependency DAG tree\n"
              << "  \033[32mkill\033[0m    <service> [sig] Send signal (e.g., 9 for SIGKILL, 11 for SIGSEGV)\n"
              << "  \033[32mlogs\033[0m    <service> [n]   Fetch the most recent stdout/stderr output lines\n"
              << "  \033[32mreload\033[0m                  Reload service configuration files from disk\n"
              << "  \033[32mhelp\033[0m                    Show this help guide\n\n"
              << "\033[1mOPTIONS:\033[0m\n"
              << "  -s, --socket <path>     Path to daemon UNIX socket (default: /tmp/processpilot.sock)\n"
              << "  -h, --help              Show help manual\n";
}

int main(int argc, char* argv[]) {
    std::string socketPath = "/tmp/processpilot.sock";
    std::vector<std::string> args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-s" || arg == "--socket") && i + 1 < argc) {
            socketPath = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            printHelp();
            return 0;
        } else {
            args.push_back(arg);
        }
    }

    if (args.empty()) {
        printHelp();
        return 0;
    }

    std::string command = args[0];
    std::string fullCommand = command;
    for (size_t i = 1; i < args.size(); ++i) {
        fullCommand += " " + args[i];
    }

    processpilot::UnixSocketClient client(socketPath);
    std::string response = client.sendCommand(fullCommand);

    if (response.rfind("ERROR:", 0) == 0) {
        std::cerr << "\033[31m" << response << "\033[0m" << std::endl;
        return 1;
    } else if (response.rfind("SUCCESS:", 0) == 0) {
        std::cout << "\033[32m" << response << "\033[0m" << std::endl;
        return 0;
    } else {
        std::cout << response << std::endl;
        return 0;
    }
}
