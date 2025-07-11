#ifndef BURWELL_CPL_EXECUTOR_H
#define BURWELL_CPL_EXECUTOR_H

#include "cpl_parser.h"
#include "command_library.h"
#include "../ocal/ocal.h"
#include <functional>
#include <chrono>
#include <future>
#include <atomic>
#include <queue>
#include <mutex>

namespace burwell {
namespace cpl {

struct ExecutionContext {
    std::string executionId;
    std::string userId;
    std::map<std::string, std::string> variables;
    std::map<std::string, std::string> environment;
    bool dryRun;
    bool stopOnError;
    int maxRetries;
    double timeoutMs;
    std::chrono::system_clock::time_point startTime;
};

struct ExecutionResult {
    std::string executionId;
    std::string commandId;
    std::string commandType;
    bool success;
    std::string errorMessage;
    std::map<std::string, std::string> outputs;
    double executionTimeMs;
    int retryCount;
    std::chrono::system_clock::time_point timestamp;
    
    // OS feedback data
    std::string osResponse;
    bool osOperationSucceeded;
    std::map<std::string, std::string> systemState;
};

struct SequenceExecutionResult {
    std::string executionId;
    std::string sequenceName;
    std::vector<ExecutionResult> commandResults;
    bool overallSuccess;
    std::string errorMessage;
    double totalExecutionTimeMs;
    int commandsExecuted;
    int commandsSucceeded;
    int commandsFailed;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
};

enum class ExecutionMode {
    SYNCHRONOUS,    // Execute commands one by one, wait for completion
    ASYNCHRONOUS,   // Execute commands in background
    STEP_BY_STEP,   // Execute one command, wait for user confirmation
    SIMULATION      // Don't execute, just validate and estimate
};

enum class ExecutionPriority {
    LOW,
    NORMAL,
    HIGH,
    CRITICAL
};

class CPLExecutor {
public:
    CPLExecutor();
    ~CPLExecutor();
    
    // Initialization
    bool initialize(std::shared_ptr<OCAL> ocal, CommandLibraryManager* library);
    void shutdown();
    
    // Single command execution
    ExecutionResult executeCommand(const CPLCommand& command, 
                                 const ExecutionContext& context = ExecutionContext{});
    std::future<ExecutionResult> executeCommandAsync(const CPLCommand& command,
                                                    const ExecutionContext& context = ExecutionContext{});
    
    // Sequence execution
    SequenceExecutionResult executeSequence(const std::vector<CPLCommand>& commands,
                                           const ExecutionContext& context = ExecutionContext{});
    SequenceExecutionResult executeSequence(const std::string& sequenceName,
                                           const ExecutionContext& context = ExecutionContext{});
    std::future<SequenceExecutionResult> executeSequenceAsync(const std::vector<CPLCommand>& commands,
                                                             const ExecutionContext& context = ExecutionContext{});
    
    // Execution control
    void pauseExecution(const std::string& executionId);
    void resumeExecution(const std::string& executionId);
    void cancelExecution(const std::string& executionId);
    void cancelAllExecutions();
    
    // Execution modes and settings
    void setExecutionMode(ExecutionMode mode);
    void setDefaultTimeout(double timeoutMs);
    void setMaxRetries(int retries);
    void setStopOnError(bool stopOnError);
    
    // Feedback and learning
    void setFeedbackHandler(std::function<void(const ExecutionResult&)> handler);
    void setSequenceFeedbackHandler(std::function<void(const SequenceExecutionResult&)> handler);
    void setOSFeedbackCollector(std::function<std::map<std::string, std::string>()> collector);
    
    // Status and monitoring
    std::vector<std::string> getActiveExecutions();
    ExecutionResult getExecutionStatus(const std::string& executionId);
    std::vector<ExecutionResult> getExecutionHistory(int limit = 100);
    bool isExecuting() const;
    
    // Validation and simulation
    std::vector<std::string> validateSequence(const std::vector<CPLCommand>& commands);
    SequenceExecutionResult simulateSequence(const std::vector<CPLCommand>& commands);
    double estimateExecutionTime(const std::vector<CPLCommand>& commands);
    
    // Variable and environment management
    void setVariable(const std::string& name, const std::string& value);
    std::string getVariable(const std::string& name);
    void setEnvironmentVariable(const std::string& name, const std::string& value);
    void clearVariables();
    
    // Error handling and recovery
    void setErrorHandler(std::function<bool(const ExecutionResult&)> handler);
    void addRetryStrategy(const std::string& commandType, 
                         std::function<bool(const CPLCommand&, int)> strategy);
    
    // Performance and optimization
    void enablePerformanceCollection(bool enable);
    nlohmann::json getPerformanceMetrics();
    void optimizeCommandExecution(const std::string& commandType);

private:
    // Core execution methods
    ExecutionResult executeMouseCommand(const CPLCommand& command, const ExecutionContext& context);
    ExecutionResult executeKeyboardCommand(const CPLCommand& command, const ExecutionContext& context);
    ExecutionResult executeWindowCommand(const CPLCommand& command, const ExecutionContext& context);
    ExecutionResult executeApplicationCommand(const CPLCommand& command, const ExecutionContext& context);
    ExecutionResult executeSystemCommand(const CPLCommand& command, const ExecutionContext& context);
    ExecutionResult executeControlFlowCommand(const CPLCommand& command, const ExecutionContext& context);
    
    // Execution helpers
    std::string generateExecutionId();
    ExecutionContext createDefaultContext();
    bool shouldRetryCommand(const CPLCommand& command, const ExecutionResult& result, int retryCount);
    void collectSystemFeedback(ExecutionResult& result);
    void recordExecutionMetrics(const ExecutionResult& result);
    
    // Variable substitution
    CPLCommand substituteVariables(const CPLCommand& command, const ExecutionContext& context);
    std::string substituteString(const std::string& input, const ExecutionContext& context);
    
    // Async execution management
    void workerThreadFunction();
    void processExecutionQueue();
    
    // Error handling
    bool handleExecutionError(const CPLCommand& command, ExecutionResult& result, int retryCount);
    void logExecutionResult(const ExecutionResult& result);
    
    // Performance monitoring
    void updatePerformanceMetrics(const ExecutionResult& result);
    void optimizeBasedOnMetrics();
    
    // Member variables
    std::shared_ptr<OCAL> m_ocal;
    CommandLibraryManager* m_library;
    
    // Execution state
    ExecutionMode m_executionMode;
    double m_defaultTimeoutMs;
    int m_maxRetries;
    bool m_stopOnError;
    std::atomic<bool> m_isRunning;
    
    // Async execution
    std::thread m_workerThread;
    std::queue<std::function<void()>> m_executionQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    
    // Active executions tracking
    std::map<std::string, std::atomic<bool>> m_activeExecutions;
    std::map<std::string, std::atomic<bool>> m_pausedExecutions;
    std::mutex m_executionsMutex;
    
    // Execution history
    std::vector<ExecutionResult> m_executionHistory;
    std::mutex m_historyMutex;
    int m_maxHistorySize;
    
    // Variables and environment
    std::map<std::string, std::string> m_variables;
    std::map<std::string, std::string> m_environment;
    std::mutex m_variablesMutex;
    
    // Feedback handlers
    std::function<void(const ExecutionResult&)> m_feedbackHandler;
    std::function<void(const SequenceExecutionResult&)> m_sequenceFeedbackHandler;
    std::function<std::map<std::string, std::string>()> m_osFeedbackCollector;
    std::function<bool(const ExecutionResult&)> m_errorHandler;
    
    // Retry strategies
    std::map<std::string, std::function<bool(const CPLCommand&, int)>> m_retryStrategies;
    
    // Performance tracking
    bool m_performanceCollectionEnabled;
    std::map<std::string, std::vector<double>> m_performanceData;
    std::mutex m_performanceMutex;
    
    // Configuration
    bool m_collectFeedback;
    bool m_autoOptimize;
    int m_maxConcurrentExecutions;
};

// Utility functions
std::string executionResultToString(const ExecutionResult& result);
nlohmann::json executionResultToJson(const ExecutionResult& result);
std::string sequenceExecutionResultToString(const SequenceExecutionResult& result);

} // namespace cpl
} // namespace burwell

#endif // BURWELL_CPL_EXECUTOR_H