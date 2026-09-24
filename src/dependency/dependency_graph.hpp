#pragma once

#include "config/config_parser.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace processpilot {

struct DependencyNode {
    std::string name;
    std::vector<std::string> dependencies;   // services that this node depends on (must start BEFORE this)
    std::vector<std::string> dependents;     // services that depend on THIS node (must start AFTER this)
    bool isHardRequirement{false};           // requires vs after
};

class DependencyGraph {
public:
    DependencyGraph() = default;

    void buildGraph(const std::vector<ServiceConfig>& configs);
    void addService(const ServiceConfig& config);
    void removeService(const std::string& name);

    bool hasCycles(std::vector<std::string>& cyclePath) const;
    std::vector<std::string> getStartupOrder() const;
    std::vector<std::string> getShutdownOrder() const;

    // Query immediate dependencies for a given service
    std::vector<std::string> getDependencies(const std::string& name) const;
    // Query services that depend on this service
    std::vector<std::string> getDependents(const std::string& name) const;

    // Print ASCII graph representation for CLI/logs
    std::string toAsciiTree() const;

    bool contains(const std::string& name) const {
        return nodes_.find(name) != nodes_.end();
    }

private:
    bool dfsCycle(const std::string& current,
                  std::unordered_map<std::string, int>& visitState,
                  std::vector<std::string>& path) const;

    std::unordered_map<std::string, DependencyNode> nodes_;
};

} // namespace processpilot
