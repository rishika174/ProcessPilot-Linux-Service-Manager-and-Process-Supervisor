#include "ipc/unix_socket.hpp"
#include "logging/logger.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>

namespace processpilot {

UnixSocketServer::UnixSocketServer(const std::string& socketPath)
    : socketPath_(socketPath) {}

UnixSocketServer::~UnixSocketServer() {
    stop();
}

bool UnixSocketServer::start(CommandHandler handler) {
    handler_ = handler;

    // Remove existing socket file if present
    unlink(socketPath_.c_str());

    serverFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        LOG_ERROR("IPC", "Failed to create UNIX domain socket: " + std::string(strerror(errno)));
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(serverFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("IPC", "Failed to bind socket at " + socketPath_ + ": " + strerror(errno));
        close(serverFd_);
        serverFd_ = -1;
        return false;
    }

    if (listen(serverFd_, 10) < 0) {
        LOG_ERROR("IPC", "Failed to listen on socket: " + std::string(strerror(errno)));
        close(serverFd_);
        serverFd_ = -1;
        return false;
    }

    isRunning_ = true;
    listenerThread_ = std::thread(&UnixSocketServer::listenerLoop, this);
    LOG_INFO("IPC", "UNIX socket IPC server listening on " + socketPath_);
    return true;
}

void UnixSocketServer::stop() {
    if (!isRunning_) return;
    isRunning_ = false;

    if (serverFd_ >= 0) {
        shutdown(serverFd_, SHUT_RDWR);
        close(serverFd_);
        serverFd_ = -1;
    }

    if (listenerThread_.joinable()) {
        listenerThread_.join();
    }

    unlink(socketPath_.c_str());
    LOG_INFO("IPC", "UNIX socket IPC server stopped");
}

void UnixSocketServer::listenerLoop() {
    while (isRunning_) {
        struct sockaddr_un clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientFd = accept(serverFd_, (struct sockaddr*)&clientAddr, &len);
        if (clientFd < 0) {
            if (!isRunning_) break;
            continue;
        }

        // Handle client connection
        handleClient(clientFd);
    }
}

void UnixSocketServer::handleClient(int clientFd) {
    char buffer[4096];
    std::string request;

    // Read until newline or EOF
    while (true) {
        ssize_t bytes = read(clientFd, buffer, sizeof(buffer) - 1);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        request += buffer;
        if (request.find('\n') != std::string::npos) break;
    }

    // Strip trailing newline/whitespace
    while (!request.empty() && (request.back() == '\n' || request.back() == '\r')) {
        request.pop_back();
    }

    std::string response;
    if (handler_) {
        response = handler_(request);
    } else {
        response = "ERR: No handler registered\n";
    }

    if (response.empty() || response.back() != '\n') {
        response += "\n";
    }

    write(clientFd, response.c_str(), response.length());
    close(clientFd);
}

// ==================== Client Implementation ====================

UnixSocketClient::UnixSocketClient(const std::string& socketPath)
    : socketPath_(socketPath) {}

bool UnixSocketClient::connectToServer() {
    clientFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (clientFd_ < 0) return false;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(clientFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(clientFd_);
        clientFd_ = -1;
        return false;
    }

    return true;
}

void UnixSocketClient::disconnect() {
    if (clientFd_ >= 0) {
        close(clientFd_);
        clientFd_ = -1;
    }
}

std::string UnixSocketClient::sendCommand(const std::string& command) {
    if (!connectToServer()) {
        return "ERROR: Could not connect to ProcessPilot daemon at " + socketPath_ + ". Is 'processpilotd' running?";
    }

    std::string cmd = command;
    if (cmd.empty() || cmd.back() != '\n') cmd += "\n";

    if (write(clientFd_, cmd.c_str(), cmd.length()) < 0) {
        disconnect();
        return "ERROR: Failed to transmit command to daemon";
    }

    std::string response;
    char buffer[4096];
    ssize_t bytes = 0;
    while ((bytes = read(clientFd_, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes] = '\0';
        response += buffer;
    }

    disconnect();
    return response;
}

} // namespace processpilot
