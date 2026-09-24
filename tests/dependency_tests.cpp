#include "dependency/dependency_graph.hpp"
#include <iostream>
#include <cassert>
#include <algorithm>

using namespace processpilot;

void testAcyclicStartupOrder() {
    std::cout << "[TEST] Running testAcyclicStartupOrder...\n";
    DependencyGraph graph;

    ServiceConfig db;
    db.name = "database";
    db.execStart = "mock-db";

    ServiceConfig cache;
    cache.name = "cache";
    cache.after = {"database"};
    cache.execStart = "mock-cache";

    ServiceConfig api;
    api.name = "web-api";
    api.after = {"database", "cache"};
    api.execStart = "mock-api";

    ServiceConfig worker;
    worker.name = "worker";
    worker.after = {"cache"};
    worker.execStart = "mock-worker";

    graph.buildGraph({db, cache, api, worker});

    std::vector<std::string> cycle;
    assert(!graph.hasCycles(cycle));

    auto order = graph.getStartupOrder();
    std::cout << "  Startup sequence: ";
    for (const auto& s : order) std::cout << s << " -> ";
    std::cout << "DONE\n";

    // DB must appear before Cache and Web API
    auto dbIt = std::find(order.begin(), order.end(), "database");
    auto cacheIt = std::find(order.begin(), order.end(), "cache");
    auto apiIt = std::find(order.begin(), order.end(), "web-api");
    auto workerIt = std::find(order.begin(), order.end(), "worker");

    assert(dbIt < cacheIt);
    assert(dbIt < apiIt);
    assert(cacheIt < apiIt);
    assert(cacheIt < workerIt);

    std::cout << "  -> PASSED: Correct topological startup sequence verified\n";
}

void testCycleDetection() {
    std::cout << "[TEST] Running testCycleDetection...\n";
    DependencyGraph graph;

    ServiceConfig s1;
    s1.name = "srv-A";
    s1.after = {"srv-C"};
    s1.execStart = "cmdA";

    ServiceConfig s2;
    s2.name = "srv-B";
    s2.after = {"srv-A"};
    s2.execStart = "cmdB";

    ServiceConfig s3;
    s3.name = "srv-C";
    s3.after = {"srv-B"}; // Cycle: A -> C -> B -> A
    s3.execStart = "cmdC";

    graph.buildGraph({s1, s2, s3});

    std::vector<std::string> cycle;
    bool hasCycle = graph.hasCycles(cycle);
    assert(hasCycle == true);
    assert(!cycle.empty());

    std::cout << "  Cycle detected correctly: ";
    for (const auto& s : cycle) std::cout << s << " -> ";
    std::cout << "\n";
    std::cout << "  -> PASSED: Cyclic dependency successfully caught\n";
}

int main() {
    std::cout << "==========================================\n";
    std::cout << "  Running ProcessPilot Dependency Tests   \n";
    std::cout << "==========================================\n";
    testAcyclicStartupOrder();
    testCycleDetection();
    std::cout << "\nALL DEPENDENCY TESTS PASSED!\n";
    return 0;
}
