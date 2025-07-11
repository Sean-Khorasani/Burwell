#ifndef BURWELL_ISTATE_MANAGER_H
#define BURWELL_ISTATE_MANAGER_H

#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../common/types.h"

namespace burwell {

// Forward declaration
struct ExecutionContext;

/**
 * @brief Interface for state management
 * 
 * Provides a common interface for both regular and thread-safe state managers.
 * This enables dependency injection and runtime selection of implementation.
 */
class IStateManager {
public:
    virtual ~IStateManager() = default;
    
    // Configuration
    virtual void setMaxCompletedExecutions(size_t maxExecutions) = 0;
    
    // Request management
    virtual std::string createRequest(const std::string& userInput) = 0;
    virtual bool hasRequest(const std::string& requestId) const = 0;
    virtual void removeRequest(const std::string& requestId) = 0;
    
    // Execution context management
    virtual ExecutionContext& getExecutionContext(const std::string& requestId) = 0;
    virtual const ExecutionContext& getExecutionContext(const std::string& requestId) const = 0;
    virtual void createExecutionContext(const std::string& requestId, const std::string& userInput) = 0;
    virtual void updateExecutionContext(const std::string& requestId, const ExecutionContext& context) = 0;
    
    // Active executions
    virtual void markExecutionActive(const std::string& requestId) = 0;
    virtual void markExecutionComplete(const std::string& requestId, const TaskExecutionResult& result) = 0;
    virtual bool isExecutionActive(const std::string& requestId) const = 0;
    virtual std::vector<std::string> getActiveRequests() const = 0;
    virtual size_t getActiveExecutionCount() const = 0;
    
    // Completed executions
    virtual TaskExecutionResult getExecutionResult(const std::string& requestId) const = 0;
    virtual bool hasExecutionResult(const std::string& requestId) const = 0;
    virtual std::vector<std::string> getCompletedRequests() const = 0;
    virtual void cleanupCompletedExecutions() = 0;
    
    // Variable management
    virtual void setVariable(const std::string& requestId, const std::string& name, const nlohmann::json& value) = 0;
    virtual nlohmann::json getVariable(const std::string& requestId, const std::string& name) const = 0;
    virtual bool hasVariable(const std::string& requestId, const std::string& name) const = 0;
    virtual void inheritVariables(const std::string& fromRequestId, const std::string& toRequestId) = 0;
    
    // Script stack management
    virtual void pushScript(const std::string& requestId, const std::string& scriptPath) = 0;
    virtual void popScript(const std::string& requestId) = 0;
    virtual bool isScriptInStack(const std::string& requestId, const std::string& scriptPath) const = 0;
    virtual int getScriptNestingLevel(const std::string& requestId) const = 0;
    
    // Sub-script results
    virtual void setSubScriptResult(const std::string& requestId, const std::string& scriptName, const nlohmann::json& result) = 0;
    virtual nlohmann::json getSubScriptResult(const std::string& requestId, const std::string& scriptName) const = 0;
    
    // Activity logging
    virtual void logActivity(const std::string& activity) = 0;
    virtual std::vector<std::string> getRecentActivity() const = 0;
    virtual void setMaxActivityLogSize(size_t maxSize) = 0;
    
    // State persistence
    virtual nlohmann::json exportState() const = 0;
    virtual void importState(const nlohmann::json& state) = 0;
};

/**
 * @brief Adapter for the regular StateManager
 * 
 * Wraps the existing StateManager to implement the IStateManager interface.
 */
class StateManagerAdapter : public IStateManager {
public:
    explicit StateManagerAdapter(std::shared_ptr<StateManager> impl);
    ~StateManagerAdapter() override = default;
    
    // Implement all IStateManager methods by delegating to m_impl
    void setMaxCompletedExecutions(size_t maxExecutions) override;
    std::string createRequest(const std::string& userInput) override;
    bool hasRequest(const std::string& requestId) const override;
    void removeRequest(const std::string& requestId) override;
    ExecutionContext& getExecutionContext(const std::string& requestId) override;
    const ExecutionContext& getExecutionContext(const std::string& requestId) const override;
    void createExecutionContext(const std::string& requestId, const std::string& userInput) override;
    void updateExecutionContext(const std::string& requestId, const ExecutionContext& context) override;
    void markExecutionActive(const std::string& requestId) override;
    void markExecutionComplete(const std::string& requestId, const TaskExecutionResult& result) override;
    bool isExecutionActive(const std::string& requestId) const override;
    std::vector<std::string> getActiveRequests() const override;
    size_t getActiveExecutionCount() const override;
    TaskExecutionResult getExecutionResult(const std::string& requestId) const override;
    bool hasExecutionResult(const std::string& requestId) const override;
    std::vector<std::string> getCompletedRequests() const override;
    void cleanupCompletedExecutions() override;
    void setVariable(const std::string& requestId, const std::string& name, const nlohmann::json& value) override;
    nlohmann::json getVariable(const std::string& requestId, const std::string& name) const override;
    bool hasVariable(const std::string& requestId, const std::string& name) const override;
    void inheritVariables(const std::string& fromRequestId, const std::string& toRequestId) override;
    void pushScript(const std::string& requestId, const std::string& scriptPath) override;
    void popScript(const std::string& requestId) override;
    bool isScriptInStack(const std::string& requestId, const std::string& scriptPath) const override;
    int getScriptNestingLevel(const std::string& requestId) const override;
    void setSubScriptResult(const std::string& requestId, const std::string& scriptName, const nlohmann::json& result) override;
    nlohmann::json getSubScriptResult(const std::string& requestId, const std::string& scriptName) const override;
    void logActivity(const std::string& activity) override;
    std::vector<std::string> getRecentActivity() const override;
    void setMaxActivityLogSize(size_t maxSize) override;
    nlohmann::json exportState() const override;
    void importState(const nlohmann::json& state) override;
    
private:
    std::shared_ptr<StateManager> m_impl;
};

/**
 * @brief Factory method for creating state managers
 * 
 * @param threadSafe If true, creates thread-safe implementation
 * @return Shared pointer to IStateManager
 */
std::shared_ptr<IStateManager> createStateManager(bool threadSafe = true);

} // namespace burwell

#endif // BURWELL_ISTATE_MANAGER_H