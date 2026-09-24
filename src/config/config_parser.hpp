#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace processpilot {

enum class RestartPolicy {
    NO,
    ALWAYS,
    ON_FAILURE,
    ON_ABORT
};

struct ServiceConfig {
    std::string name;             // Derived from filename (e.g., "demo")
    std::string description;      // Human description
    std::vector<std::string> after;    // Startup dependencies
    std::vector<std::string> requires; // Hard dependencies

    std::string execStart;        // Command to run
    std::string execStop;         // Optional shutdown command
    std::string workingDirectory; // Optional working dir
    std::string user;             // Optional user
    
    RestartPolicy restartPolicy{RestartPolicy::ON_FAILURE};
    int restartSec{3};            // Backoff delay in seconds
    int maxRestarts{5};           // Max crash restarts in window
    int restartWindowSec{60};     // Time window for counting restarts
    bool autoStart{true};         // Auto-start with daemon

    std::unordered_map<std::string, std::string> environment;
    std::string stdOutPath;
    std::string stdErrPath;

    static RestartPolicy parseRestartPolicy(const std::string& str);
    static std::string restartPolicyToString(RestartPolicy policy);
};

class ConfigParser {
public:
    static std::optional<ServiceConfig> parseFile(const std::string& filepath);
    static std::optional<ServiceConfig> parseString(const std::string& serviceName, const std::string& content);
    static std::vector<ServiceConfig> parseDirectory(const std::string& dirpath);

private:
    static std::string trim(const std::string& str);
    static std::vector<std::string> splitList(const std::string& str);
};

} // namespace processpilot
