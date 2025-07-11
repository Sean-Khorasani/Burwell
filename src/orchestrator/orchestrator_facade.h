#ifndef BURWELL_ORCHESTRATOR_FACADE_H
#define BURWELL_ORCHESTRATOR_FACADE_H

#include <memory>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>
#include "../common/types.h"
#include "event_manager.h"
#include <queue>
#include <condition_variable>

namespace burwell {

// Forward declarations
class CommandParser;
class LLMConnector;
class TaskEngine;
class OCAL;
class EnvironmentalPerception;
class UIModule;
class ExecutionEngine;
class StateManager;
class EventManager;
class ScriptManager;
class FeedbackController;
class ConversationManager;

/**
 * @class OrchestratorFacade
 * @brief Simplified interface to the orchestration system
 * 
 * This class provides the main interface to the orchestration system,
 * coordinating all the extracted components. It replaces the original
 * monolithic Orchestrator class.
 */
class OrchestratorFacade {
public:
    OrchestratorFacade();
    ~OrchestratorFacade();

    // Main orchestration interface
    void initialize();
    void run();
    void shutdown();

    // Request processing
    TaskExecutionResult processUserRequest(const std::string& userInput);
    TaskExecutionResult executeTask(const std::string& taskId);
    TaskExecutionResult executePlan(const nlohmann::json& executionPlan);
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
    void setMainLoopDelayMs(int delayMs);
    void setCommandSequenceDelayMs(int delayMs);
    void setErrorRecoveryDelayMs(int delayMs);

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
    bool isIdle() const;
    nlohmann::json getSystemStatus() const;
    std::vector<std::string> getRecentActivity() const;

    // Error recovery control
    void setErrorRecoveryEnabled(bool enabled);
    void setMaxErrorRetries(int maxRetries);

    // Feedback control
    void setFeedbackLoopEnabled(bool enabled);
    void setEnvironmentCheckInterval(int intervalMs);

private:
    // Core components (injected dependencies)
    std::shared_ptr<CommandParser> m_commandParser;
    std::shared_ptr<LLMConnector> m_llmConnector;
    std::shared_ptr<TaskEngine> m_taskEngine;
    std::shared_ptr<OCAL> m_ocal;
    std::shared_ptr<EnvironmentalPerception> m_perception;
    std::shared_ptr<UIModule> m_ui;

    // Internal components (owned by facade)
    std::unique_ptr<ExecutionEngine> m_executionEngine;
    std::unique_ptr<StateManager> m_stateManager;
    std::unique_ptr<EventManager> m_eventManager;
    std::unique_ptr<ScriptManager> m_scriptManager;
    std::unique_ptr<FeedbackController> m_feedbackController;
    std::unique_ptr<ConversationManager> m_conversationManager;

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
    bool m_errorRecoveryEnabled;
    int m_maxErrorRetries;
    bool m_feedbackLoopEnabled;

    // Request processing
    std::queue<std::string> m_requestQueue;
    std::thread m_workerThread;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;

    // Internal methods
    void initializeComponents();
    void connectComponents();
    void workerThreadFunction();
    void processRequestQueue();
    TaskExecutionResult processRequestInternal(const std::string& requestId, const std::string& userInput);

    // Workflow coordination
    TaskExecutionResult parseUserRequest(const std::string& userInput, const std::string& requestId);
    TaskExecutionResult generateExecutionPlan(const std::string& userInput, const std::string& requestId);
    TaskExecutionResult executeWithErrorRecovery(const nlohmann::json& plan, const std::string& requestId);

    // Error handling
    void handleExecutionError(const std::string& requestId, const std::string& error);
    TaskExecutionResult attemptErrorRecovery(const std::string& requestId, const std::string& error);

    // Utility methods
    void logActivity(const std::string& activity);
    void updateSystemStatus();
    bool canProcessNewRequest() const;
};

} // namespace burwell

#endif // BURWELL_ORCHESTRATOR_FACADE_H