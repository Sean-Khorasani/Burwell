#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include "common/structured_logger.h"

using namespace burwell;
using namespace std::chrono_literals;

void testBasicLogging() {
    std::cout << "[TEST] Basic Logging\n";
    
    auto& logger = StructuredLogger::getInstance();
    
    // Test different log levels
    logger.debug().message("Debug message").context("key", "value");
    logger.info().message("Info message");
    logger.warning().message("Warning message");
    logger.error().message("Error message");
    logger.critical().message("Critical message");
    
    std::cout << "[OK] Basic logging test passed\n\n";
}

void testFileLogging() {
    std::cout << "[TEST] File Logging with Rotation\n";
    
    auto& logger = StructuredLogger::getInstance();
    
    // Add file sink with rotation
    RotatingFileLogSink::Config config;
    config.base_path = "test_logs/app.log";
    config.max_file_size = 1024 * 1024;  // 1MB
    config.max_files = 3;
    
    auto formatter = std::make_shared<JsonLogFormatter>();
    auto fileSink = std::make_shared<RotatingFileLogSink>(config, formatter);
    logger.addSink(fileSink);
    
    // Log some messages
    for (int i = 0; i < 10; ++i) {
        logger.info()
            .message("Test file logging")
            .context("iteration", i)
            .context("timestamp", std::chrono::system_clock::now().time_since_epoch().count());
    }
    
    std::cout << "[OK] File logging test passed\n\n";
}

void testPerformanceTracking() {
    std::cout << "[TEST] Performance Tracking\n";
    
    auto& logger = StructuredLogger::getInstance();
    auto& tracker = logger.getPerformanceTracker();
    
    // Simulate some operations
    for (int i = 0; i < 5; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(50ms + std::chrono::milliseconds(i * 10));
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        
        tracker.recordOperation("test_operation", duration);
    }
    
    // Get metrics
    auto metrics = tracker.getMetrics("test_operation");
    std::cout << "Operation count: " << metrics.count << "\n";
    std::cout << "Average duration: " << metrics.getAverageDurationMs() << " ms\n";
    
    std::cout << "[OK] Performance tracking test passed\n\n";
}

void testScopedTimer() {
    std::cout << "[TEST] Scoped Timer\n";
    
    {
        SCOPED_TIMER("scoped_operation");
        std::this_thread::sleep_for(100ms);
    }
    
    // Nested timers
    {
        SCOPED_TIMER("parent_operation");
        std::this_thread::sleep_for(50ms);
        
        {
            SCOPED_TIMER("child_operation");
            std::this_thread::sleep_for(25ms);
        }
    }
    
    std::cout << "[OK] Scoped timer test passed\n\n";
}

void testAsyncLogging() {
    std::cout << "[TEST] Async Logging\n";
    
    auto& logger = StructuredLogger::getInstance();
    logger.setAsyncLogging(true);
    
    // Log many messages quickly
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        logger.info()
            .message("Async log message")
            .context("index", i)
            .context("thread", oss.str());
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time to queue 1000 messages: " << duration.count() << " ms\n";
    
    // Give async thread time to process
    std::this_thread::sleep_for(100ms);
    
    logger.setAsyncLogging(false);
    
    std::cout << "[OK] Async logging test passed\n\n";
}

void testLogLevels() {
    std::cout << "[TEST] Log Level Filtering\n";
    
    auto& logger = StructuredLogger::getInstance();
    
    // Set to WARNING level
    logger.setLogLevel(::LogLevel::WARNING);
    
    // These should not appear
    logger.debug().message("Should not see this debug message");
    logger.info().message("Should not see this info message");
    
    // These should appear
    logger.warning().message("Should see this warning message");
    logger.error().message("Should see this error message");
    
    // Reset to INFO
    logger.setLogLevel(::LogLevel::INFO);
    
    std::cout << "[OK] Log level filtering test passed\n\n";
}

void testStructuredContext() {
    std::cout << "[TEST] Structured Context\n";
    
    auto& logger = StructuredLogger::getInstance();
    
    nlohmann::json userContext;
    userContext["user_id"] = 12345;
    userContext["session_id"] = "abc-123-def";
    userContext["permissions"] = {"read", "write"};
    
    logger.info()
        .message("User action performed")
        .context("action", "file_upload")
        .context("user", userContext)
        .context("file_size", 1024 * 1024)
        .context("success", true);
    
    std::cout << "[OK] Structured context test passed\n\n";
}

void testPeriodicReporting() {
    std::cout << "[TEST] Periodic Performance Reporting\n";
    
    auto& logger = StructuredLogger::getInstance();
    auto& tracker = logger.getPerformanceTracker();
    
    // Enable periodic reporting every 2 seconds
    tracker.enablePeriodicReporting(std::chrono::seconds(2));
    
    // Simulate operations
    for (int i = 0; i < 10; ++i) {
        auto duration = std::chrono::milliseconds(50 + i * 5);
        tracker.recordOperation("periodic_test", std::chrono::nanoseconds(duration));
        std::this_thread::sleep_for(300ms);
    }
    
    // Disable reporting
    tracker.disablePeriodicReporting();
    
    std::cout << "[OK] Periodic reporting test passed\n\n";
}

int main() {
    std::cout << "=== Burwell Structured Logger Test Suite ===\n\n";
    
    try {
        testBasicLogging();
        testFileLogging();
        testPerformanceTracking();
        testScopedTimer();
        testAsyncLogging();
        testLogLevels();
        testStructuredContext();
        testPeriodicReporting();
        
        // Shutdown logger
        StructuredLogger::getInstance().shutdown();
        
        std::cout << "\n=== All tests passed successfully! ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAILED] Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}