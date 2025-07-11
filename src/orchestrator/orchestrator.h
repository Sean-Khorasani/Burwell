#ifndef BURWELL_ORCHESTRATOR_H
#define BURWELL_ORCHESTRATOR_H

#include <memory>
#include <string>
#include <functional>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <nlohmann/json.hpp>
#include "../common/types.h"
#include "../common/resource_monitor.h"
#include "event_manager.h"

namespace burwell {

// Forward declarations
class CommandParser;
class LLMConnector;
class TaskEngine;
class OCAL;
class EnvironmentalPerception;
class UIModule;

// Execution context for tracking state during task execution
struct ExecutionContext {
    std::string requestId;
    std::string originalRequest;
    nlohmann::json currentEnvironment;
    std::vector<std::string> executionLog;
    bool requiresUserConfirmation;
    std::map<std::string, nlohmann::json> variables;  // Store variables from command results
    
    // Nested script execution support
    int nestingLevel;                                    // Current nesting depth
    int maxNestingLevel;                                 // Maximum allowed nesting depth
    std::vector<std::string> scriptStack;               // Stack of executing scripts
    std::map<std::string, nlohmann::json> subScriptResults;  // Results from executed sub-scripts
    
    ExecutionContext() : requiresUserConfirmation(false), nestingLevel(0), maxNestingLevel(3) {}
    
    // Move constructor
    ExecutionContext(ExecutionContext&& other) noexcept
        : requestId(std::move(other.requestId))
        , originalRequest(std::move(other.originalRequest))
        , currentEnvironment(std::move(other.currentEnvironment))
        , executionLog(std::move(other.executionLog))
        , requiresUserConfirmation(other.requiresUserConfirmation)
        , variables(std::move(other.variables))
        , nestingLevel(other.nestingLevel)
        , maxNestingLevel(other.maxNestingLevel)
        , scriptStack(std::move(other.scriptStack))
        , subScriptResults(std::move(other.subScriptResults)) {}
    
    // Move assignment
    ExecutionContext& operator=(ExecutionContext&& other) noexcept {
        if (this != &other) {
            requestId = std::move(other.requestId);
            originalRequest = std::move(other.originalRequest);
            currentEnvironment = std::move(other.currentEnvironment);
            executionLog = std::move(other.executionLog);
            requiresUserConfirmation = other.requiresUserConfirmation;
            variables = std::move(other.variables);
            nestingLevel = other.nestingLevel;
            maxNestingLevel = other.maxNestingLevel;
            scriptStack = std::move(other.scriptStack);
            subScriptResults = std::move(other.subScriptResults);
        }
        return *this;
    }
    
    // Copy constructor and assignment are default
    ExecutionContext(const ExecutionContext&) = default;
    ExecutionContext& operator=(const ExecutionContext&) = default;
};

// Event types are defined in event_manager.h

class Orchestrator {
public:
    Orchestrator();
    ~Orchestrator();
    
    // Main orchestration interface
    void initialize();
    void run();
    void shutdown();
    
    // Request processing
    TaskExecutionResult processUserRequest(const std::string& userInput);
    TaskExecutionResult executeTask(const std::string& taskId);
    TaskExecutionResult executePlan(const nlohmann::json& executionPlan);
    TaskExecutionResult executePlan(const nlohmann::json& executionPlan, ExecutionContext& context);
    TaskExecutionResult executeScriptFile(const std::string& scriptPath);
    
    // Async processing
    std::string processUserRequestAsync(const std::string& userInput);
    TaskExecutionResult getExecutionResult(const std::string& requestId);
    std::vector<std::string> getActiveRequests() const;
    
    // Component management
    void setCommandParser(std::shared_ptr<CommandParser> parser);
    void setLLMConnector(std::shared_ptr<LLMConnector> connector);
    void setTaskEngine(std::shared_ptr<TaskEngine> engine);
    void setOCAL(std::shared_ptr<OCAL> ocal);
    void setEnvironmentalPerception(std::shared_ptr<EnvironmentalPerception> perception);
    void setUIModule(std::shared_ptr<UIModule> ui);
    
    // Configuration
    void setAutoMode(bool enabled);
    void setConfirmationRequired(bool required);
    void setMaxConcurrentTasks(int maxTasks);
    void setExecutionTimeout(int timeoutMs);
    
    // Monitoring and control
    void pauseExecution();
    void resumeExecution();
    void cancelExecution(const std::string& requestId);
    void emergencyStop();
    
    // Event handling
    void addEventListener(std::function<void(const EventData&)> listener);
    void raiseEvent(const EventData& event);
    
    // Status and reporting
    bool isRunning() const;
    bool isPaused() const;
    bool isIdle() const;
    nlohmann::json getSystemStatus() const;
    nlohmann::json getEnvironmentSnapshot() const;
    std::vector<std::string> getRecentActivity() const;
    
    // Feedback loop control
    void enableFeedbackLoop(bool enable);
    void setEnvironmentCheckInterval(int intervalMs);
    
    // Backward compatibility methods
    std::string substituteVariables(const std::string& input, const ExecutionContext& context);
    TaskExecutionResult executeSystemCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeOCALCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeUIACommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeTaskCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeScriptCommand(const nlohmann::json& command, ExecutionContext& context);
    
    // Helper methods
    bool shouldContinueExecution() const;
    void handleExecutionError(const std::string& error, ExecutionContext& context);
    void updateEnvironmentSnapshot(ExecutionContext& context);
    bool verifyCommandResult(const nlohmann::json& command, const TaskExecutionResult& result, ExecutionContext& context);
    void logActivity(const std::string& activity);

private:
    // Implementation using Pimpl idiom for better encapsulation
    class OrchestratorImpl;
    std::unique_ptr<OrchestratorImpl> m_impl;
    
    // State management
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isPaused;
    std::atomic<bool> m_emergencyStop;
    
    // Configuration
    bool m_autoMode;
    bool m_confirmationRequired;
    int m_maxConcurrentTasks;
    int m_executionTimeoutMs;
    int m_mainLoopDelayMs;
    int m_commandSequenceDelayMs;
    int m_errorRecoveryDelayMs;
    
    // Execution management
    std::queue<std::string> m_requestQueue;
    std::map<std::string, ExecutionContext> m_activeExecutions;
    std::map<std::string, TaskExecutionResult> m_completedExecutions;
    
    // Threading
    std::thread m_workerThread;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    mutable std::mutex m_stateMutex;
    
    // Event system
    std::vector<std::function<void(const EventData&)>> m_eventListeners;
    std::mutex m_eventMutex;
    
    // Activity logging
    std::vector<std::string> m_recentActivity;
    mutable std::mutex m_activityMutex;
    
    // Internal orchestration methods
    void workerThreadFunction();
    void processRequestQueue();
    TaskExecutionResult processRequestInternal(const std::string& requestId, const std::string& userInput);
    
    // Workflow steps
    TaskExecutionResult parseUserRequest(const std::string& userInput, ExecutionContext& context);
    TaskExecutionResult generateExecutionPlan(const std::string& userInput, ExecutionContext& context);
    TaskExecutionResult executeCommandSequence(const nlohmann::json& commands, ExecutionContext& context);
    TaskExecutionResult executeCommand(const nlohmann::json& command, ExecutionContext& context);
    
    // Variable substitution helpers (declaration already exists above)
    nlohmann::json substituteVariablesInParams(const nlohmann::json& params, const ExecutionContext& context);
    
    // Variable loading and management
    void loadScriptVariables(const nlohmann::json& script, ExecutionContext& context);
    void inheritParentVariables(const ExecutionContext& parentContext, ExecutionContext& childContext);
    
    // Validation and safety
    bool validateExecutionPlan(const nlohmann::json& plan);
    bool requiresUserConfirmation(const nlohmann::json& plan);
    bool isCommandSafe(const nlohmann::json& command);
    
    // Error handling and recovery
    void handleExecutionError(const std::string& requestId, const std::string& error);
    void attemptRecovery(const std::string& requestId, const std::string& error);
    void rollbackExecution(const std::string& requestId);
    
    // Environment monitoring
    void updateEnvironmentalContext(ExecutionContext& context);
    bool detectEnvironmentChanges(const ExecutionContext& context);
    void adaptToEnvironmentChanges(ExecutionContext& context);
    
    // Intelligent Feedback Loop System
    struct FeedbackLoopState {
        nlohmann::json lastEnvironmentSnapshot;
        std::chrono::steady_clock::time_point lastEnvironmentCheck;
        nlohmann::json currentExecutionPlan;
        int environmentCheckIntervalMs;
        int adaptationThresholdMs;
        bool continuousMonitoringEnabled;
        std::vector<nlohmann::json> environmentHistory;
        std::map<std::string, int> commandSuccessRates;
        
        FeedbackLoopState() : 
            environmentCheckIntervalMs(1000),
            adaptationThresholdMs(2000),
            continuousMonitoringEnabled(true) {}
    };
    
    FeedbackLoopState m_feedbackLoop;
    std::thread m_feedbackThread;
    std::atomic<bool> m_feedbackActive;
    mutable std::mutex m_feedbackMutex;
    
    // Feedback loop methods
    void startContinuousEnvironmentMonitoring();
    void stopContinuousEnvironmentMonitoring();
    void feedbackLoopWorker();
    bool analyzeEnvironmentChanges(const nlohmann::json& currentEnv, const nlohmann::json& previousEnv);
    void adaptExecutionPlanToEnvironment(ExecutionContext& context, const nlohmann::json& environmentChanges);
    void updateCommandSuccessMetrics(const std::string& command, bool success);
    nlohmann::json generateAdaptiveCommandSuggestions(const ExecutionContext& context);
    void requestLLMEnvironmentAnalysis(ExecutionContext& context);
    
    // LLM Conversation System for dynamic environmental requests and command adaptation
    struct ConversationState {
        std::string conversationId;
        std::vector<nlohmann::json> messageHistory;
        nlohmann::json currentContext;
        bool awaitingLLMResponse;
        bool requiresEnvironmentalUpdate;
        std::chrono::steady_clock::time_point lastInteraction;
        std::map<std::string, nlohmann::json> environmentalQueries;
        int maxConversationTurns;
        
        ConversationState() : 
            awaitingLLMResponse(false),
            requiresEnvironmentalUpdate(false),
            maxConversationTurns(10) {}
    };
    
    std::map<std::string, ConversationState> m_activeConversations;
    mutable std::mutex m_conversationMutex;
    
    // Conversational LLM methods
    std::string initiateConversationalExecution(const std::string& userInput, ExecutionContext& context);
    TaskExecutionResult processConversationalTurn(const std::string& conversationId, const nlohmann::json& llmResponse);
    nlohmann::json handleEnvironmentalDataRequest(const std::string& conversationId, const nlohmann::json& request);
    TaskExecutionResult adaptCommandsBasedOnFeedback(const std::string& conversationId, const nlohmann::json& adaptationRequest);
    bool shouldRequestAdditionalEnvironmentalData(const ExecutionContext& context, const nlohmann::json& currentPlan);
    nlohmann::json generateEnvironmentalDataQuery(const ExecutionContext& context, const std::string& queryType);
    void updateConversationContext(const std::string& conversationId, const nlohmann::json& newContext);
    void cleanupExpiredConversations();
    
    // Error Recovery System for LLM re-analysis and retry
    struct ErrorRecoveryState {
        std::string originalCommand;
        nlohmann::json commandContext;
        std::vector<std::string> attemptHistory;
        int retryCount;
        int maxRetries;
        std::chrono::steady_clock::time_point lastAttempt;
        std::string lastError;
        nlohmann::json environmentSnapshot;
        bool requiresLLMAnalysis;
        
        ErrorRecoveryState() : 
            retryCount(0),
            maxRetries(3),
            requiresLLMAnalysis(false) {}
    };
    
    std::map<std::string, ErrorRecoveryState> m_errorRecoveryStates;
    mutable std::mutex m_errorRecoveryMutex;
    
    // Error recovery methods
    TaskExecutionResult initiateErrorRecovery(const std::string& failedCommand, const nlohmann::json& commandContext, const std::string& errorMessage);
    TaskExecutionResult performLLMErrorAnalysis(const std::string& recoveryId, const ErrorRecoveryState& state);
    std::vector<nlohmann::json> generateAlternativeCommands(const ErrorRecoveryState& state);
    TaskExecutionResult retryCommandWithAlternatives(const std::string& recoveryId, const std::vector<nlohmann::json>& alternatives);
    bool shouldRetryCommand(const ErrorRecoveryState& state, const std::string& currentError);
    void updateErrorRecoveryState(const std::string& recoveryId, const std::string& error, bool success);
    void cleanupErrorRecoveryStates();
    
    // User Interaction System for LLM to request user input during workflows
    struct UserInteractionRequest {
        std::string interactionId;
        std::string conversationId;
        std::string promptMessage;
        std::string inputType;  // "text", "choice", "password", "file_path", "confirmation"
        nlohmann::json inputOptions;  // For choice type, file filters, etc.
        std::chrono::steady_clock::time_point requestTime;
        std::chrono::steady_clock::time_point timeoutTime;
        bool isUrgent;
        bool hasResponse;
        nlohmann::json userResponse;
        
        UserInteractionRequest() : 
            isUrgent(false),
            hasResponse(false) {}
    };
    
    std::map<std::string, UserInteractionRequest> m_pendingUserInteractions;
    mutable std::mutex m_userInteractionMutex;
    
    // User interaction methods
    std::string requestUserInput(const std::string& conversationId, const std::string& promptMessage, const std::string& inputType, const nlohmann::json& options = nlohmann::json::object());
    TaskExecutionResult waitForUserResponse(const std::string& interactionId, int timeoutMs = 30000);
    bool provideUserResponse(const std::string& interactionId, const nlohmann::json& response);
    TaskExecutionResult handleUserInteractionRequest(const nlohmann::json& interactionRequest);
    std::vector<UserInteractionRequest> getPendingUserInteractions() const;
    void cancelUserInteraction(const std::string& interactionId);
    void cleanupExpiredUserInteractions();
    
    // Utility methods
    std::string generateRequestId();
    // void logActivity(const std::string& activity); // Already declared above
    void cleanupCompletedExecutions();
    double getCurrentTimeMs() const;
    
    // Command execution helpers (some are already declared above)
    TaskExecutionResult executeMouseCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeKeyboardCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeApplicationCommand(const nlohmann::json& command, ExecutionContext& context);
    // TaskExecutionResult executeSystemCommand(const nlohmann::json& command, ExecutionContext& context); // Already declared
    TaskExecutionResult executeWindowCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeWaitCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeControlCommand(const nlohmann::json& command, ExecutionContext& context);
    // TaskExecutionResult executeScriptCommand(const nlohmann::json& command, ExecutionContext& context); // Already declared
    
    // Loop control methods
    TaskExecutionResult executeWhileLoop(const nlohmann::json& command, ExecutionContext& context);
    bool evaluateLoopCondition(const nlohmann::json& condition, ExecutionContext& context);
    
    // Resource monitoring
    struct ResourceThresholds {
        size_t maxMemoryUsageMB = 1024;  // 1GB default
        size_t maxFileHandles = 100;
        size_t maxWindowHandles = 50;
        size_t maxThreads = 20;
        bool enableResourceTracking = true;
    };
    ResourceThresholds m_resourceThresholds;
    
    // Resource monitoring methods
    void checkResourceUsage();
    void logResourceMetrics();
    bool isResourceUsageAcceptable() const;
};

} // namespace burwell

#endif // BURWELL_ORCHESTRATOR_H


