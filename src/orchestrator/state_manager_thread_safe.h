#ifndef BURWELL_STATE_MANAGER_H
#define BURWELL_STATE_MANAGER_H

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <nlohmann/json.hpp>
#include "../common/types.h"
#include "../common/thread_safe_queue.h"

namespace burwell {

// Forward declaration
struct ExecutionContext;

/**
 * @class StateManager
 * @brief Thread-safe state management for orchestrator execution
 * 
 * Uses reader-writer locks for better concurrent read performance
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
    
    // Thread-safe context access
    template<typename Func>
    void withExecutionContext(const std::string& requestId, Func func) {
        std::unique_lock<std::shared_mutex> lock(m_contextRwMutex);
        auto it = m_executionContexts.find(requestId);
        if (it != m_executionContexts.end() && it->second) {
            func(*it->second);
        }
    }
    
    template<typename Func>
    void withExecutionContextRead(const std::string& requestId, Func func) const {
        std::shared_lock<std::shared_mutex> lock(m_contextRwMutex);
        auto it = m_executionContexts.find(requestId);
        if (it != m_executionContexts.end() && it->second) {
            func(*it->second);
        }
    }
    
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

    // Variable management (thread-safe)
    void setVariable(const std::string& requestId, const std::string& name, const nlohmann::json& value);
    nlohmann::json getVariable(const std::string& requestId, const std::string& name) const;
    bool hasVariable(const std::string& requestId, const std::string& name) const;
    void inheritVariables(const std::string& fromRequestId, const std::string& toRequestId);

    // Script stack management (thread-safe)
    void pushScript(const std::string& requestId, const std::string& scriptPath);
    void popScript(const std::string& requestId);
    bool isScriptInStack(const std::string& requestId, const std::string& scriptPath) const;
    int getScriptNestingLevel(const std::string& requestId) const;

    // Sub-script results (thread-safe)
    void setSubScriptResult(const std::string& requestId, const std::string& scriptName, const nlohmann::json& result);
    nlohmann::json getSubScriptResult(const std::string& requestId, const std::string& scriptName) const;

    // Activity logging (lock-free for readers)
    void logActivity(const std::string& activity);
    std::vector<std::string> getRecentActivity() const;
    void setMaxActivityLogSize(size_t maxSize);

    // State persistence
    nlohmann::json exportState() const;
    void importState(const nlohmann::json& state);
    
    // Statistics
    struct StateStats {
        std::atomic<size_t> totalRequests{0};
        std::atomic<size_t> activeRequests{0};
        std::atomic<size_t> completedRequests{0};
        std::atomic<size_t> failedRequests{0};
        std::atomic<size_t> variableAccesses{0};
        std::atomic<size_t> contextSwitches{0};
    };
    
    const StateStats& getStats() const { return m_stats; }

private:
    // Thread-safe state storage
    std::map<std::string, std::unique_ptr<ExecutionContext>> m_executionContexts;
    std::map<std::string, TaskExecutionResult> m_completedExecutions;
    
    // Lock-free activity log using circular buffer
    struct ActivityLog {
        static constexpr size_t DEFAULT_SIZE = 1000;
        std::vector<std::string> entries;
        std::atomic<size_t> writePos{0};
        std::atomic<size_t> size{0};
        size_t maxSize{DEFAULT_SIZE};
        
        ActivityLog() : entries(DEFAULT_SIZE) {}
        
        void push(const std::string& activity);
        std::vector<std::string> getRecent() const;
        void resize(size_t newSize);
    };
    
    ActivityLog m_activityLog;
    
    // Configuration
    std::atomic<size_t> m_maxCompletedExecutions{1000};
    
    // Thread safety - using shared_mutex for reader-writer locks
    mutable std::shared_mutex m_contextRwMutex;
    mutable std::shared_mutex m_resultRwMutex;
    
    // Request ID generation
    std::atomic<uint64_t> m_requestCounter{0};
    
    // Statistics
    mutable StateStats m_stats;
    
    // Utility methods
    std::string generateRequestId();
    void enforceCompletedExecutionsLimit();
    ExecutionContext createDefaultContext(const std::string& requestId, const std::string& userInput);
};

/**
 * @brief Thread-safe variable store with copy-on-write semantics
 */
class ThreadSafeVariableStore {
public:
    void set(const std::string& name, const nlohmann::json& value) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_variables[name] = value;
        m_version++;
    }
    
    nlohmann::json get(const std::string& name) const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_variables.find(name);
        return (it != m_variables.end()) ? it->second : nlohmann::json();
    }
    
    bool has(const std::string& name) const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_variables.find(name) != m_variables.end();
    }
    
    std::map<std::string, nlohmann::json> getAll() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_variables;
    }
    
    void clear() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_variables.clear();
        m_version++;
    }
    
    uint64_t getVersion() const {
        return m_version.load();
    }
    
private:
    mutable std::shared_mutex m_mutex;
    std::map<std::string, nlohmann::json> m_variables;
    std::atomic<uint64_t> m_version{0};
};

} // namespace burwell

#endif // BURWELL_STATE_MANAGER_H