#include "dependency/dependency_graph.hpp"
#include "logging/logger.hpp"
#include <queue>
#include <sstream>
#include <algorithm>

namespace processpilot {

void DependencyGraph::buildGraph(const std::vector<ServiceConfig>& configs) {
    nodes_.clear();
    for (const auto& config : configs) {
        addService(config);
    }
}

void DependencyGraph::addService(const ServiceConfig& config) {
    // Ensure node exists
    if (nodes_.find(config.name) == nodes_.end()) {
        nodes_[config.name] = DependencyNode{config.name, {}, {}, false};
    }

    // Add 'after' dependencies
    for (const auto& dep : config.after) {
        if (dep.empty()) continue;
        nodes_[config.name].dependencies.push_back(dep);

        // Ensure prerequisite node exists
        if (nodes_.find(dep) == nodes_.end()) {
            nodes_[dep] = DependencyNode{dep, {}, {}, false};
        }
        nodes_[dep].dependents.push_back(config.name);
    }

    // Add 'requires' dependencies
    for (const auto& dep : config.requires) {
        if (dep.empty()) continue;
        // Avoid duplicate in dependencies list
        auto& dList = nodes_[config.name].dependencies;
        if (std::find(dList.begin(), dList.end(), dep) == dList.end()) {
            dList.push_back(dep);
        }

        if (nodes_.find(dep) == nodes_.end()) {
            nodes_[dep] = DependencyNode{dep, {}, {}, true};
        }
        auto& depChildren = nodes_[dep].dependents;
        if (std::find(depChildren.begin(), depChildren.end(), config.name) == depChildren.end()) {
            depChildren.push_back(config.name);
        }
    }
}

void DependencyGraph::removeService(const std::string& name) {
    if (nodes_.find(name) == nodes_.end()) return;

    // Remove from other nodes' dependencies and dependents
    for (auto& [nodeName, node] : nodes_) {
        node.dependencies.erase(
            std::remove(node.dependencies.begin(), node.dependencies.end(), name),
            node.dependencies.end()
        );
        node.dependents.erase(
            std::remove(node.dependents.begin(), node.dependents.end(), name),
            node.dependents.end()
        );
    }

    nodes_.erase(name);
}

bool DependencyGraph::dfsCycle(const std::string& current,
                              std::unordered_map<std::string, int>& visitState,
                              std::vector<std::string>& path) const {
    visitState[current] = 1; // 1 = visiting (in recursion stack)
    path.push_back(current);

    auto it = nodes_.find(current);
    if (it != nodes_.end()) {
        for (const auto& neighbor : it->second.dependencies) {
            // Check if neighbor is in recursion stack -> cycle detected
            if (visitState[neighbor] == 1) {
                path.push_back(neighbor);
                return true;
            }
            if (visitState[neighbor] == 0) {
                if (dfsCycle(neighbor, visitState, path)) {
                    return true;
                }
            }
        }
    }

    visitState[current] = 2; // 2 = visited
    path.pop_back();
    return false;
}

bool DependencyGraph::hasCycles(std::vector<std::string>& cyclePath) const {
    std::unordered_map<std::string, int> visitState; // 0 = unvisited, 1 = visiting, 2 = visited
    for (const auto& [name, _] : nodes_) {
        visitState[name] = 0;
    }

    for (const auto& [name, _] : nodes_) {
        if (visitState[name] == 0) {
            std::vector<std::string> path;
            if (dfsCycle(name, visitState, path)) {
                cyclePath = path;
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> DependencyGraph::getStartupOrder() const {
    // Kahn's Algorithm for Topological Sorting
    std::unordered_map<std::string, int> inDegree;
    for (const auto& [name, node] : nodes_) {
        inDegree[name] = node.dependencies.size();
    }

    std::queue<std::string> q;
    for (const auto& [name, degree] : inDegree) {
        if (degree == 0) {
            q.push(name);
        }
    }

    std::vector<std::string> order;
    while (!q.empty()) {
        std::string current = q.front();
        q.pop();
        order.push_back(current);

        auto it = nodes_.find(current);
        if (it != nodes_.end()) {
            for (const auto& dependent : it->second.dependents) {
                inDegree[dependent]--;
                if (inDegree[dependent] == 0) {
                    q.push(dependent);
                }
            }
        }
    }

    if (order.size() != nodes_.size()) {
        LOG_WARN("DependencyGraph", "Cycle detected during topological sort; order may be partial.");
        // Append any remaining unvisited nodes
        for (const auto& [name, _] : nodes_) {
            if (std::find(order.begin(), order.end(), name) == order.end()) {
                order.push_back(name);
            }
        }
    }

    return order;
}

std::vector<std::string> DependencyGraph::getShutdownOrder() const {
    auto startup = getStartupOrder();
    std::reverse(startup.begin(), startup.end());
    return startup;
}

std::vector<std::string> DependencyGraph::getDependencies(const std::string& name) const {
    auto it = nodes_.find(name);
    if (it != nodes_.end()) {
        return it->second.dependencies;
    }
    return {};
}

std::vector<std::string> DependencyGraph::getDependents(const std::string& name) const {
    auto it = nodes_.find(name);
    if (it != nodes_.end()) {
        return it->second.dependents;
    }
    return {};
}

std::string DependencyGraph::toAsciiTree() const {
    std::ostringstream ss;
    ss << "=== Service Dependency Hierarchy ===\n";

    // Find root nodes (no dependencies)
    std::vector<std::string> roots;
    for (const auto& [name, node] : nodes_) {
        if (node.dependencies.empty()) {
            roots.push_back(name);
        }
    }

    if (roots.empty() && !nodes_.empty()) {
        // Cyclic or all have deps
        for (const auto& [name, _] : nodes_) roots.push_back(name);
    }

    for (const auto& root : roots) {
        ss << "  [" << root << "]\n";
        auto it = nodes_.find(root);
        if (it != nodes_.end()) {
            for (size_t i = 0; i < it->second.dependents.size(); ++i) {
                bool isLast = (i == it->second.dependents.size() - 1);
                ss << (isLast ? "  └── " : "  ├── ") << it->second.dependents[i] << "\n";
            }
        }
    }

    return ss.str();
}

} // namespace processpilot
