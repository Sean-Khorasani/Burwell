#ifndef BURWELL_TYPES_H
#define BURWELL_TYPES_H

#include <string>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

namespace burwell {

// Execution status types
enum class ExecutionStatus {
    PENDING,
    IN_PROGRESS,
    COMPLETED,
    FAILED,
    CANCELLED,
    WAITING_FOR_INPUT,
    BREAK_LOOP,
    CONTINUE_LOOP
};

// Task execution result
struct TaskExecutionResult {
    std::string executionId;
    ExecutionStatus status;
    bool success;
    std::string errorMessage;
    std::string output;  // For command output
    nlohmann::json result;
    std::chrono::milliseconds executionTime;
    double executionTimeMs;  // Keep for compatibility
    std::vector<std::string> executedCommands;
    
    TaskExecutionResult() : status(ExecutionStatus::PENDING), success(false), 
                           executionTime(0), executionTimeMs(0.0) {}
};

} // namespace burwell

#endif // BURWELL_TYPES_H