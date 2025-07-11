#ifndef BURWELL_STATE_MANAGER_H
#define BURWELL_STATE_MANAGER_H

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>
#include "../common/types.h"

namespace burwell {

// Forward declaration
struct ExecutionContext;

/**
 * @class StateManager
 * @brief Manages execution state, context, and request lifecycle
 * 
 * This class handles all state management functionality including active
 * executions, completed executions, and execution contexts.
 */
class StateManager {
public:
    StateManager();
    ~StateManager();

    // Configuration
    void setMaxCompletedExecutions(size_t maxExecutions);

    // Request management
    std::string createRequest(const std::string& userInput);
    bool hasRequest(const std::string& requestId) const;
    void removeRequest(const std::string& requestId);
    
    // Execution context management
    ExecutionContext& getExecutionContext(const std::string& requestId);
    const ExecutionContext& getExecutionContext(const std::string& requestId) const;
    void createExecutionContext(const std::string& requestId, const std::string& userInput);
    void updateExecutionContext(const std::string& requestId, const ExecutionContext& context);
    
    // Active executions
    void markExecutionActive(const std::string& requestId);
    void markExecutionComplete(const std::string& requestId, const TaskExecutionResult& result);
    bool isExecutionActive(const std::string& requestId) const;
    std::vector<std::string> getActiveRequests() const;
    size_t getActiveExecutionCount() const;

    // Completed executions
    TaskExecutionResult getExecutionResult(const std::string& requestId) const;
    bool hasExecutionResult(const std::string& requestId) const;
    std::vector<std::string> getCompletedRequests() const;
    void cleanupCompletedExecutions();

    // Variable management
    void setVariable(const std::string& requestId, const std::string& name, const nlohmann::json& value);
    nlohmann::json getVariable(const std::string& requestId, const std::string& name) const;
    bool hasVariable(const std::string& requestId, const std::string& name) const;
    void inheritVariables(const std::string& fromRequestId, const std::string& toRequestId);

    // Script stack management
    void pushScript(const std::string& requestId, const std::string& scriptPath);
    void popScript(const std::string& requestId);
    bool isScriptInStack(const std::string& requestId, const std::string& scriptPath) const;
    int getScriptNestingLevel(const std::string& requestId) const;

    // Sub-script results
    void setSubScriptResult(const std::string& requestId, const std::string& scriptName, const nlohmann::json& result);
    nlohmann::json getSubScriptResult(const std::string& requestId, const std::string& scriptName) const;

    // Activity logging
    void logActivity(const std::string& activity);
    std::vector<std::string> getRecentActivity() const;
    void setMaxActivityLogSize(size_t maxSize);

    // State persistence (future enhancement)
    nlohmann::json exportState() const;
    void importState(const nlohmann::json& state);

private:
    // State storage
    std::map<std::string, ExecutionContext> m_executionContexts;
    std::map<std::string, TaskExecutionResult> m_completedExecutions;
    std::vector<std::string> m_recentActivity;
    
    // Configuration
    size_t m_maxCompletedExecutions;
    size_t m_maxActivityLogSize;
    
    // Thread safety
    mutable std::mutex m_contextMutex;
    mutable std::mutex m_resultMutex;
    mutable std::mutex m_activityMutex;
    
    // Utility methods
    std::string generateRequestId();
    void enforceCompletedExecutionsLimit();
    void enforceActivityLogLimit();
    ExecutionContext createDefaultContext(const std::string& requestId, const std::string& userInput);
};

} // namespace burwell

#endif // BURWELL_STATE_MANAGER_H