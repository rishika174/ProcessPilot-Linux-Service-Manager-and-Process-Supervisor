#include "config/config_parser.hpp"
#include <iostream>
#include <cassert>

using namespace processpilot;

void testValidConfigParsing() {
    std::cout << "[TEST] Running testValidConfigParsing...\n";
    std::string sample = R"(
[Unit]
Description=High Performance Cache Daemon
After=network.target database
Requires=database

[Service]
ExecStart=/usr/bin/redis-server --port 6379
Restart=always
RestartSec=5
MaxRestarts=10
RestartWindowSec=120
AutoStart=true
Environment=PORT=6379
Environment=LOG_LEVEL=debug
StandardOutput=file:/var/log/redis.log
)";

    auto cfg = ConfigParser::parseString("cache", sample);
    assert(cfg.has_value());
    assert(cfg->name == "cache");
    assert(cfg->description == "High Performance Cache Daemon");
    assert(cfg->after.size() == 2);
    assert(cfg->after[0] == "network.target");
    assert(cfg->after[1] == "database");
    assert(cfg->requires.size() == 1);
    assert(cfg->requires[0] == "database");
    assert(cfg->execStart == "/usr/bin/redis-server --port 6379");
    assert(cfg->restartPolicy == RestartPolicy::ALWAYS);
    assert(cfg->restartSec == 5);
    assert(cfg->maxRestarts == 10);
    assert(cfg->restartWindowSec == 120);
    assert(cfg->autoStart == true);
    assert(cfg->environment["PORT"] == "6379");
    assert(cfg->environment["LOG_LEVEL"] == "debug");

    std::cout << "  -> PASSED: Valid config parsed completely with all fields\n";
}

void testMissingExecStartFails() {
    std::cout << "[TEST] Running testMissingExecStartFails...\n";
    std::string invalid = R"(
[Unit]
Description=Broken Service
)";

    auto cfg = ConfigParser::parseString("broken", invalid);
    assert(!cfg.has_value());
    std::cout << "  -> PASSED: Properly rejected config without ExecStart\n";
}

int main() {
    std::cout << "==========================================\n";
    std::cout << "  Running ProcessPilot Config Parser Tests\n";
    std::cout << "==========================================\n";
    testValidConfigParsing();
    testMissingExecStartFails();
    std::cout << "\nALL CONFIG TESTS PASSED!\n";
    return 0;
}
