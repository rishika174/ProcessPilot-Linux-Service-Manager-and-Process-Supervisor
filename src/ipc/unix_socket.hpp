#pragma once

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <sys/un.h>

namespace processpilot {

class UnixSocketServer {
public:
    using CommandHandler = std::function<std::string(const std::string& request)>;

    explicit UnixSocketServer(const std::string& socketPath);
    ~UnixSocketServer();

    bool start(CommandHandler handler);
    void stop();
    bool isRunning() const { return isRunning_; }

private:
    std::string socketPath_;
    int serverFd_{-1};
    std::atomic<bool> isRunning_{false};
    std::thread listenerThread_;
    CommandHandler handler_;

    void listenerLoop();
    void handleClient(int clientFd);
};

class UnixSocketClient {
public:
    explicit UnixSocketClient(const std::string& socketPath = "/tmp/processpilot.sock");

    bool connectToServer();
    void disconnect();
    std::string sendCommand(const std::string& command);

private:
    std::string socketPath_;
    int clientFd_{-1};
};

} // namespace processpilot
