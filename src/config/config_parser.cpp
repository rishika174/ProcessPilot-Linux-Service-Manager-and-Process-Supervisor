#include "config/config_parser.hpp"
#include "logging/logger.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace processpilot {

RestartPolicy ServiceConfig::parseRestartPolicy(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "always") return RestartPolicy::ALWAYS;
    if (lower == "on-failure") return RestartPolicy::ON_FAILURE;
    if (lower == "on-abort") return RestartPolicy::ON_ABORT;
    return RestartPolicy::NO;
}

std::string ServiceConfig::restartPolicyToString(RestartPolicy policy) {
    switch (policy) {
        case RestartPolicy::ALWAYS: return "always";
        case RestartPolicy::ON_FAILURE: return "on-failure";
        case RestartPolicy::ON_ABORT: return "on-abort";
        case RestartPolicy::NO: default: return "no";
    }
}

std::string ConfigParser::trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> ConfigParser::splitList(const std::string& str) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ' ')) {
        std::string trimmed = trim(item);
        if (!trimmed.empty()) {
            // strip commas if present
            if (trimmed.back() == ',') trimmed.pop_back();
            if (!trimmed.empty()) {
                result.push_back(trimmed);
            }
        }
    }
    return result;
}

std::optional<ServiceConfig> ConfigParser::parseFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("Config", "Cannot open service config file: " + filepath);
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    // Extract base service name without extension
    std::filesystem::path p(filepath);
    std::string serviceName = p.stem().string();

    return parseString(serviceName, buffer.str());
}

std::optional<ServiceConfig> ConfigParser::parseString(const std::string& serviceName, const std::string& content) {
    ServiceConfig config;
    config.name = serviceName;

    std::istringstream stream(content);
    std::string line;
    std::string currentSection = "";
    int lineNum = 0;

    while (std::getline(stream, line)) {
        lineNum++;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue; // Comment or empty line
        }

        // Section header: [Unit], [Service], [Install]
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            currentSection = trimmed.substr(1, trimmed.length() - 2);
            continue;
        }

        // Key = Value
        auto eqPos = trimmed.find('=');
        if (eqPos == std::string::npos) {
            LOG_WARN("Config", "Malformed line " + std::to_string(lineNum) + " in " + serviceName + ": " + trimmed);
            continue;
        }

        std::string key = trim(trimmed.substr(0, eqPos));
        std::string value = trim(trimmed.substr(eqPos + 1));

        if (currentSection == "Unit") {
            if (key == "Description") {
                config.description = value;
            } else if (key == "After") {
                config.after = splitList(value);
            } else if (key == "Requires") {
                config.requires = splitList(value);
            }
        } else if (currentSection == "Service") {
            if (key == "ExecStart") {
                config.execStart = value;
            } else if (key == "ExecStop") {
                config.execStop = value;
            } else if (key == "WorkingDirectory") {
                config.workingDirectory = value;
            } else if (key == "User") {
                config.user = value;
            } else if (key == "Restart") {
                config.restartPolicy = ServiceConfig::parseRestartPolicy(value);
            } else if (key == "RestartSec") {
                try { config.restartSec = std::stoi(value); } catch (...) {}
            } else if (key == "MaxRestarts") {
                try { config.maxRestarts = std::stoi(value); } catch (...) {}
            } else if (key == "RestartWindowSec") {
                try { config.restartWindowSec = std::stoi(value); } catch (...) {}
            } else if (key == "AutoStart") {
                config.autoStart = (value == "true" || value == "1" || value == "yes");
            } else if (key == "Environment") {
                auto colonPos = value.find('=');
                if (colonPos != std::string::npos) {
                    std::string envKey = trim(value.substr(0, colonPos));
                    std::string envVal = trim(value.substr(colonPos + 1));
                    config.environment[envKey] = envVal;
                }
            } else if (key == "StandardOutput") {
                config.stdOutPath = value;
            } else if (key == "StandardError") {
                config.stdErrPath = value;
            }
        }
    }

    if (config.execStart.empty()) {
        LOG_ERROR("Config", "Service " + serviceName + " has no ExecStart command!");
        return std::nullopt;
    }

    if (config.description.empty()) {
        config.description = "Managed service " + serviceName;
    }

    return config;
}

std::vector<ServiceConfig> ConfigParser::parseDirectory(const std::string& dirpath) {
    std::vector<ServiceConfig> configs;
    std::filesystem::path p(dirpath);

    if (!std::filesystem::exists(p) || !std::filesystem::is_directory(p)) {
        LOG_WARN("Config", "Directory does not exist: " + dirpath);
        return configs;
    }

    for (const auto& entry : std::filesystem::directory_iterator(p)) {
        if (entry.is_regular_file() && entry.path().extension() == ".service") {
            auto cfg = parseFile(entry.path().string());
            if (cfg) {
                configs.push_back(*cfg);
                LOG_INFO("Config", "Loaded configuration for service: " + cfg->name);
            }
        }
    }

    return configs;
}

} // namespace processpilot
