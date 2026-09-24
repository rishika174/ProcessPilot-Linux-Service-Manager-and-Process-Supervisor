#include "process/process_manager.hpp"
#include <iostream>
#include <cassert>
#include <unistd.h>

using namespace processpilot;

void testServiceRegistration() {
    std::cout << "[TEST] Running testServiceRegistration...\n";
    ProcessManager pm;
    
    ServiceConfig config;
    config.name = "test-worker";
    config.execStart = "sleep 10";
    config.restartPolicy = RestartPolicy::ON_FAILURE;
    config.restartSec = 1;

    assert(pm.registerService(config));
    auto info = pm.getServiceInfo("test-worker");
    assert(info.has_value());
    assert(info->name == "test-worker");
    assert(info->state == ProcessState::STOPPED);
    std::cout << "  -> PASSED: Registration and initial state correct\n";
}

void testServiceLifecycle() {
    std::cout << "[TEST] Running testServiceLifecycle...\n";
    ProcessManager pm;
    
    ServiceConfig config;
    config.name = "short-sleeper";
    config.execStart = "sleep 5";
    config.restartPolicy = RestartPolicy::NO;

    pm.registerService(config);
    assert(pm.startService("short-sleeper"));
    
    auto info = pm.getServiceInfo("short-sleeper");
    assert(info.has_value());
    assert(info->state == ProcessState::RUNNING);
    assert(info->pid > 0);
    std::cout << "  -> Service running with PID: " << info->pid << "\n";

    // Stop service
    assert(pm.stopService("short-sleeper", 2));
    info = pm.getServiceInfo("short-sleeper");
    assert(info->state == ProcessState::STOPPED);
    assert(info->pid == 0);
    std::cout << "  -> PASSED: Clean startup and shutdown verified\n";
}

void testSignalHandlingAndCrashDetection() {
    std::cout << "[TEST] Running testSignalHandlingAndCrashDetection...\n";
    ProcessManager pm;
    
    ServiceConfig config;
    config.name = "crash-app";
    config.execStart = "sleep 20";
    config.restartPolicy = RestartPolicy::ON_FAILURE;
    config.restartSec = 2;

    pm.registerService(config);
    pm.startService("crash-app");

    auto info = pm.getServiceInfo("crash-app");
    assert(info && info->pid > 0);
    pid_t pid = info->pid;

    // Simulate crash by sending SIGSEGV
    kill(pid, SIGSEGV);
    usleep(200000); // 200ms

    pm.reapChildren();

    info = pm.getServiceInfo("crash-app");
    assert(info.has_value());
    // Should be detected as crashed and set to RESTARTING due to policy ON_FAILURE
    assert(info->state == ProcessState::RESTARTING);
    assert(info->restartCount == 1);
    assert(info->lastTermSignal == SIGSEGV);

    std::cout << "  -> PASSED: Crash detected via waitpid, SIGSEGV captured, auto-restart queued\n";
}

int main() {
    std::cout << "==========================================\n";
    std::cout << "  Running ProcessPilot Process Tests      \n";
    std::cout << "==========================================\n";
    testServiceRegistration();
    testServiceLifecycle();
    testSignalHandlingAndCrashDetection();
    std::cout << "\nALL PROCESS TESTS PASSED!\n";
    return 0;
}
