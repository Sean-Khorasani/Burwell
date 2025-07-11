#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include "orchestrator/state_manager_thread_safe.h"
#include "common/thread_pool.h"
#include "common/logger.h"

using namespace burwell;

// Test concurrent reads and writes to state manager
void testConcurrentStateAccess() {
    std::cout << "\n[TEST] Testing concurrent state access with reader-writer locks\n";
    
    StateManager stateManager;
    ThreadPool pool(8);  // 8 worker threads
    
    const int NUM_REQUESTS = 100;
    const int NUM_OPERATIONS = 1000;
    
    // Create some requests
    std::vector<std::string> requestIds;
    for (int i = 0; i < NUM_REQUESTS; ++i) {
        requestIds.push_back(stateManager.createRequest("Request " + std::to_string(i)));
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Launch many concurrent operations
    std::vector<std::future<void>> futures;
    
    // 70% read operations (benefit from shared locks)
    for (int i = 0; i < NUM_OPERATIONS * 0.7; ++i) {
        futures.push_back(pool.submit([&stateManager, &requestIds, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, NUM_REQUESTS - 1);
            
            std::string requestId = requestIds[dis(gen)];
            
            // Random read operation
            int op = i % 4;
            switch (op) {
                case 0:
                    stateManager.isExecutionActive(requestId);
                    break;
                case 1:
                    stateManager.hasVariable(requestId, "test_var");
                    break;
                case 2:
                    stateManager.getScriptNestingLevel(requestId);
                    break;
                case 3:
                    stateManager.getRecentActivity();
                    break;
            }
        }));
    }
    
    // 30% write operations
    for (int i = 0; i < NUM_OPERATIONS * 0.3; ++i) {
        futures.push_back(pool.submit([&stateManager, &requestIds, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, NUM_REQUESTS - 1);
            
            std::string requestId = requestIds[dis(gen)];
            
            // Random write operation
            int op = i % 3;
            switch (op) {
                case 0:
                    stateManager.setVariable(requestId, "var_" + std::to_string(i), i);
                    break;
                case 1:
                    stateManager.pushScript(requestId, "script_" + std::to_string(i) + ".json");
                    break;
                case 2:
                    stateManager.logActivity("Activity " + std::to_string(i));
                    break;
            }
        }));
    }
    
    // Wait for all operations to complete
    for (auto& future : futures) {
        future.wait();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "[RESULT] Completed " << NUM_OPERATIONS << " concurrent operations in " 
              << duration.count() << "ms\n";
    
    // Print statistics
    const auto& stats = stateManager.getStats();
    std::cout << "[STATS] Total requests: " << stats.totalRequests << "\n";
    std::cout << "[STATS] Variable accesses: " << stats.variableAccesses << "\n";
    std::cout << "[STATS] Context switches: " << stats.contextSwitches << "\n";
}

// Test thread pool with different priorities
void testThreadPoolPriorities() {
    std::cout << "\n[TEST] Testing thread pool with priority support\n";
    
    ThreadPool pool(4);
    std::atomic<int> executionOrder{0};
    std::vector<int> results(10, -1);
    
    // Submit tasks with different priorities
    std::vector<std::future<void>> futures;
    
    // Submit low priority tasks
    for (int i = 0; i < 3; ++i) {
        futures.push_back(pool.submit(ThreadPool::Priority::LOW, [&executionOrder, &results, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            results[i] = executionOrder++;
            std::cout << "[LOW] Task " << i << " executed at position " << results[i] << "\n";
        }));
    }
    
    // Submit critical priority tasks (should execute first)
    for (int i = 3; i < 6; ++i) {
        futures.push_back(pool.submit(ThreadPool::Priority::CRITICAL, [&executionOrder, &results, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            results[i] = executionOrder++;
            std::cout << "[CRITICAL] Task " << i << " executed at position " << results[i] << "\n";
        }));
    }
    
    // Submit normal priority tasks
    for (int i = 6; i < 10; ++i) {
        futures.push_back(pool.submit(ThreadPool::Priority::NORMAL, [&executionOrder, &results, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            results[i] = executionOrder++;
            std::cout << "[NORMAL] Task " << i << " executed at position " << results[i] << "\n";
        }));
    }
    
    // Wait for all tasks
    for (auto& future : futures) {
        future.wait();
    }
    
    // Get pool statistics
    auto stats = pool.getStats();
    std::cout << "\n[POOL STATS]\n";
    std::cout << "  Total tasks executed: " << stats.totalTasksExecuted << "\n";
    std::cout << "  Average task time: " 
              << std::chrono::duration_cast<std::chrono::microseconds>(stats.averageTaskTime).count() 
              << " microseconds\n";
}

// Test async task executor with cancellation
void testAsyncTaskCancellation() {
    std::cout << "\n[TEST] Testing async task executor with cancellation\n";
    
    AsyncTaskExecutor executor(2);
    
    // Submit a long-running task
    auto taskId = executor.submitTask([](std::atomic<bool>& shouldStop) {
        std::cout << "[TASK] Long running task started\n";
        for (int i = 0; i < 10; ++i) {
            if (shouldStop) {
                std::cout << "[TASK] Task cancelled at iteration " << i << "\n";
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "[TASK] Iteration " << i << "\n";
        }
        std::cout << "[TASK] Task completed normally\n";
    });
    
    // Let it run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    
    // Cancel the task
    std::cout << "[MAIN] Cancelling task...\n";
    bool cancelled = executor.cancelTask(taskId);
    std::cout << "[MAIN] Cancel request: " << (cancelled ? "SUCCESS" : "FAILED") << "\n";
    
    // Wait for task to finish
    executor.waitForTask(taskId);
    std::cout << "[MAIN] Task finished\n";
}

int main() {
    // Initialize logger
    Logger::setLogLevel(LogLevel::INFO);
    
    std::cout << "=== Burwell Threading Improvements Test ===\n";
    
    try {
        // Test 1: Concurrent state access
        testConcurrentStateAccess();
        
        // Test 2: Thread pool priorities
        testThreadPoolPriorities();
        
        // Test 3: Async task cancellation
        testAsyncTaskCancellation();
        
        std::cout << "\n[SUCCESS] All threading tests completed successfully!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Test failed: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}