#include "state_manager_thread_safe.h"
#include "../common/structured_logger.h"
#include "../common/types.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace burwell {

// ExecutionContext structure definition for thread-safe state manager
struct ExecutionContext {
    std::string requestId;
    std::string originalRequest;
    nlohmann::json currentEnvironment;
    std::vector<std::string> executionLog;
    bool requiresUserConfirmation;
    std::unique_ptr<ThreadSafeVariableStore> variables;  // Using pointer for thread-safe variable store
    
    // Nested script execution support
    int nestingLevel;
    int maxNestingLevel;
    std::vector<std::string> scriptStack;
    std::map<std::string, nlohmann::json> subScriptResults;
    
    // Additional fields for state management
    ExecutionStatus status;
    std::string errorMessage;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    int commandIndex;
    bool shouldBreak;
    
    ExecutionContext(const std::string& id, const std::string& request) 
        : requestId(id)
        , originalRequest(request)
        , requiresUserConfirmation(false)
        , variables(std::make_unique<ThreadSafeVariableStore>())
        , nestingLevel(0)
        , maxNestingLevel(3)
        , status(ExecutionStatus::PENDING)
        , commandIndex(0)
        , shouldBreak(false)
        , startTime(std::chrono::steady_clock::now()) {}
        
    // Custom copy constructor
    ExecutionContext(const ExecutionContext& other)
        : requestId(other.requestId)
        , originalRequest(other.originalRequest)
        , currentEnvironment(other.currentEnvironment)
        , executionLog(other.executionLog)
        , requiresUserConfirmation(other.requiresUserConfirmation)
        , variables(std::make_unique<ThreadSafeVariableStore>())
        , nestingLevel(other.nestingLevel)
        , maxNestingLevel(other.maxNestingLevel)
        , scriptStack(other.scriptStack)
        , subScriptResults(other.subScriptResults)
        , status(other.status)
        , errorMessage(other.errorMessage)
        , startTime(other.startTime)
        , endTime(other.endTime)
        , commandIndex(other.commandIndex)
        , shouldBreak(other.shouldBreak) {
        if (other.variables) {
            // Copy variables
            auto allVars = other.variables->getAll();
            for (const auto& [key, value] : allVars) {
                variables->set(key, value);
            }
        }
    }
    
    // Custom move constructor
    ExecutionContext(ExecutionContext&& other) noexcept = default;
    
    // Custom assignment operator
    ExecutionContext& operator=(const ExecutionContext& other) {
        if (this != &other) {
            requestId = other.requestId;
            originalRequest = other.originalRequest;
            currentEnvironment = other.currentEnvironment;
            executionLog = other.executionLog;
            requiresUserConfirmation = other.requiresUserConfirmation;
            
            // Create new variable store and copy data
            variables = std::make_unique<ThreadSafeVariableStore>();
            if (other.variables) {
                auto allVars = other.variables->getAll();
                for (const auto& [key, value] : allVars) {
                    variables->set(key, value);
                }
            }
            
            nestingLevel = other.nestingLevel;
            maxNestingLevel = other.maxNestingLevel;
            scriptStack = other.scriptStack;
            subScriptResults = other.subScriptResults;
            status = other.status;
            errorMessage = other.errorMessage;
            startTime = other.startTime;
            endTime = other.endTime;
            commandIndex = other.commandIndex;
            shouldBreak = other.shouldBreak;
        }
        return *this;
    }
    
    // Move assignment
    ExecutionContext& operator=(ExecutionContext&& other) noexcept = default;
};

// ActivityLog implementation
void StateManager::ActivityLog::push(const std::string& activity) {
    size_t pos = writePos.fetch_add(1) % maxSize;
    entries[pos] = activity;
    
    size_t currentSize = size.load();
    if (currentSize < maxSize) {
        size.compare_exchange_weak(currentSize, currentSize + 1);
    }
}

std::vector<std::string> StateManager::ActivityLog::getRecent() const {
    std::vector<std::string> result;
    size_t currentSize = size.load();
    size_t startPos = (writePos.load() - currentSize) % maxSize;
    
    for (size_t i = 0; i < currentSize; ++i) {
        size_t pos = (startPos + i) % maxSize;
        if (!entries[pos].empty()) {
            result.push_back(entries[pos]);
        }
    }
    
    return result;
}

void StateManager::ActivityLog::resize(size_t newSize) {
    // Note: This is not thread-safe and should be called before heavy usage
    std::vector<std::string> newEntries(newSize);
    size_t currentSize = std::min(size.load(), newSize);
    
    for (size_t i = 0; i < currentSize; ++i) {
        size_t oldPos = (writePos.load() - currentSize + i) % maxSize;
        size_t newPos = i;
        newEntries[newPos] = entries[oldPos];
    }
    
    entries = std::move(newEntries);
    maxSize = newSize;
    writePos = currentSize % newSize;
    size = currentSize;
}

// StateManager implementation
StateManager::StateManager() {
    SLOG_INFO().message("[STATE_MANAGER] Thread-safe state manager initialized");
}

StateManager::~StateManager() {
    SLOG_INFO().message("[STATE_MANAGER] State manager shutdown");
}

void StateManager::setMaxCompletedExecutions(size_t maxExecutions) {
    m_maxCompletedExecutions = maxExecutions;
    SLOG_DEBUG().message("[STATE_MANAGER] Max completed executions set")
        .context("max_executions", maxExecutions);
}

std::string StateManager::createRequest(const std::string& userInput) {
    std::string requestId = generateRequestId();
    
    {
        std::unique_lock<std::shared_mutex> lock(m_contextRwMutex);
        m_executionContexts[requestId] = std::make_unique<ExecutionContext>(requestId, userInput);
    }
    
    m_stats.totalRequests++;
    logActivity("[REQUEST_CREATED] ID: " + requestId);
    
    SLOG_INFO().message("[STATE_MANAGER] Created request")
        .context("request_id", requestId);
    return requestId;
}

bool StateManager::hasRequest(const std::string& requestId) const {
    std::shared_lock<std::shared_mutex> lock(m_contextRwMutex);
    return m_executionContexts.find(requestId) != m_executionContexts.end();
}

void StateManager::removeRequest(const std::string& requestId) {
    {
        std::unique_lock<std::shared_mutex> lock(m_contextRwMutex);
        m_executionContexts.erase(requestId);
    }
    
    logActivity("[REQUEST_REMOVED] ID: " + requestId);
    SLOG_DEBUG().message("[STATE_MANAGER] Removed request")
        .context("request_id", requestId);
}

ExecutionContext& StateManager::getExecutionContext(const std::string& requestId) {
    std::unique_lock<std::shared_mutex> lock(m_contextRwMutex);
    
    auto it = m_executionContexts.find(requestId);
    if (it == m_executionContexts.end() || !it->second) {
        throw std::runtime_error("Request ID not found: " + requestId);
    }
    
    m_stats.contextSwitches++;
    return *it->second;
}

const ExecutionContext& StateManager::getExecutionContext(const std::string& requestId) const {
    std::shared_lock<std::shared_mutex> lock(m_contextRwMutex);
    
    auto it = m_executionContexts.find(requestId);
    if (it == m_executionContexts.end() || !it->second) {
        throw std::runtime_error("Request ID not found: " + requestId);
    }
    
    return *it->second;
}

void StateManager::createExecutionContext(const std::string& requestId, const std::string& userInput) {
    std::unique_lock<std::shared_mutex> lock(m_contextRwMutex);
    
    if (m_executionContexts.find(requestId) == m_executionContexts.end()) {
        m_executionContexts[requestId] = std::make_unique<ExecutionContext>(requestId, userInput);
    } else {
        SLOG_WARNING().message("[STATE_MANAGER] Execution context already exists")
            .context("request_id", requestId);
    }
}

void StateManager::updateExecutionContext(const std::string& requestId, const ExecutionContext& context) {
    std::unique_lock<std::shared_mutex> lock(m_contextRwMutex);
    
    auto it = m_executionContexts.find(requestId);
    if (it != m_executionContexts.end() && it->second) {
        *it->second = context;
        m_stats.contextSwitches++;
    }
}

void StateManager::markExecutionActive(const std::string& requestId) {
    withExecutionContext(requestId, [](ExecutionContext& context) {
        context.status = ExecutionStatus::IN_PROGRESS;
        context.startTime = std::chrono::steady_clock::now();
    });
    
    m_stats.activeRequests++;
    logActivity("[EXECUTION_STARTED] ID: " + requestId);
    
    SLOG_INFO().message("[STATE_MANAGER] Marked execution active")
        .context("request_id", requestId);
}

void StateManager::markExecutionComplete(const std::string& requestId, const TaskExecutionResult& result) {
    // Update execution context
    withExecutionContext(requestId, [&result](ExecutionContext& context) {
        context.status = result.status;
        context.errorMessage = result.errorMessage;
        context.endTime = std::chrono::steady_clock::now();
    });
    
    // Store in completed executions
    {
        std::unique_lock<std::shared_mutex> lock(m_resultRwMutex);
        m_completedExecutions[requestId] = result;
        enforceCompletedExecutionsLimit();
    }
    
    // Update statistics
    m_stats.activeRequests--;
    m_stats.completedRequests++;
    if (result.status == ExecutionStatus::FAILED) {
        m_stats.failedRequests++;
    }
    
    logActivity("[EXECUTION_COMPLETED] ID: " + requestId + " Status: " + 
                std::to_string(static_cast<int>(result.status)));
    
    SLOG_INFO().message("[STATE_MANAGER] Marked execution complete")
        .context("request_id", requestId);
}

bool StateManager::isExecutionActive(const std::string& requestId) const {
    bool active = false;
    
    withExecutionContextRead(requestId, [&active](const ExecutionContext& context) {
        active = (context.status == ExecutionStatus::IN_PROGRESS);
    });
    
    return active;
}

std::vector<std::string> StateManager::getActiveRequests() const {
    std::vector<std::string> activeRequests;
    std::shared_lock<std::shared_mutex> lock(m_contextRwMutex);
    
    for (const auto& pair : m_executionContexts) {
        if (pair.second && pair.second->status == ExecutionStatus::IN_PROGRESS) {
            activeRequests.push_back(pair.first);
        }
    }
    
    return activeRequests;
}

size_t StateManager::getActiveExecutionCount() const {
    return m_stats.activeRequests.load();
}

TaskExecutionResult StateManager::getExecutionResult(const std::string& requestId) const {
    std::shared_lock<std::shared_mutex> lock(m_resultRwMutex);
    
    auto it = m_completedExecutions.find(requestId);
    if (it != m_completedExecutions.end()) {
        return it->second;
    }
    
    TaskExecutionResult notFound;
    notFound.status = ExecutionStatus::FAILED;
    notFound.errorMessage = "Result not found";
    return notFound;
}

bool StateManager::hasExecutionResult(const std::string& requestId) const {
    std::shared_lock<std::shared_mutex> lock(m_resultRwMutex);
    return m_completedExecutions.find(requestId) != m_completedExecutions.end();
}

std::vector<std::string> StateManager::getCompletedRequests() const {
    std::vector<std::string> completedRequests;
    std::shared_lock<std::shared_mutex> lock(m_resultRwMutex);
    
    for (const auto& pair : m_completedExecutions) {
        completedRequests.push_back(pair.first);
    }
    
    return completedRequests;
}

void StateManager::cleanupCompletedExecutions() {
    std::unique_lock<std::shared_mutex> lock(m_resultRwMutex);
    
    // Keep only the most recent executions
    if (m_completedExecutions.size() > m_maxCompletedExecutions) {
        // Create vector of request IDs sorted by completion time
        std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> completionTimes;
        
        for (const auto& pair : m_completedExecutions) {
            auto it = m_executionContexts.find(pair.first);
            if (it != m_executionContexts.end()) {
                if (it->second) {
                    completionTimes.emplace_back(pair.first, it->second->endTime);
                }
            }
        }
        
        // Sort by completion time (oldest first)
        std::sort(completionTimes.begin(), completionTimes.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });
        
        // Remove oldest executions
        size_t toRemove = m_completedExecutions.size() - m_maxCompletedExecutions;
        for (size_t i = 0; i < toRemove && i < completionTimes.size(); ++i) {
            m_completedExecutions.erase(completionTimes[i].first);
        }
    }
    
    logActivity("[CLEANUP] Removed old completed executions");
}

void StateManager::setVariable(const std::string& requestId, const std::string& name, 
                               const nlohmann::json& value) {
    withExecutionContext(requestId, [&name, &value](ExecutionContext& context) {
        context.variables->set(name, value);
    });
    
    m_stats.variableAccesses++;
    SLOG_DEBUG().message("[STATE_MANAGER] Set variable")
        .context("variable", name)
        .context("request_id", requestId);
}

nlohmann::json StateManager::getVariable(const std::string& requestId, const std::string& name) const {
    nlohmann::json value;
    
    withExecutionContextRead(requestId, [&name, &value](const ExecutionContext& context) {
        value = context.variables->get(name);
    });
    
    m_stats.variableAccesses++;
    return value;
}

bool StateManager::hasVariable(const std::string& requestId, const std::string& name) const {
    bool exists = false;
    
    withExecutionContextRead(requestId, [&name, &exists](const ExecutionContext& context) {
        exists = context.variables->has(name);
    });
    
    return exists;
}

void StateManager::inheritVariables(const std::string& fromRequestId, const std::string& toRequestId) {
    std::map<std::string, nlohmann::json> variables;
    
    // Get variables from source context
    withExecutionContextRead(fromRequestId, [&variables](const ExecutionContext& context) {
        variables = context.variables->getAll();
    });
    
    // Set variables in destination context
    withExecutionContext(toRequestId, [&variables](ExecutionContext& context) {
        for (const auto& pair : variables) {
            context.variables->set(pair.first, pair.second);
        }
    });
    
    SLOG_DEBUG().message("[STATE_MANAGER] Variables inherited")
        .context("count", variables.size())
        .context("from_request", fromRequestId)
        .context("to_request", toRequestId);
}

void StateManager::pushScript(const std::string& requestId, const std::string& scriptPath) {
    withExecutionContext(requestId, [&scriptPath](ExecutionContext& context) {
        context.scriptStack.push_back(scriptPath);
    });
    
    logActivity("[SCRIPT_PUSH] Request: " + requestId + " Script: " + scriptPath);
}

void StateManager::popScript(const std::string& requestId) {
    withExecutionContext(requestId, [](ExecutionContext& context) {
        if (!context.scriptStack.empty()) {
            context.scriptStack.pop_back();
        }
    });
    
    logActivity("[SCRIPT_POP] Request: " + requestId);
}

bool StateManager::isScriptInStack(const std::string& requestId, const std::string& scriptPath) const {
    bool found = false;
    
    withExecutionContextRead(requestId, [&scriptPath, &found](const ExecutionContext& context) {
        found = std::find(context.scriptStack.begin(), context.scriptStack.end(), scriptPath) 
                != context.scriptStack.end();
    });
    
    return found;
}

int StateManager::getScriptNestingLevel(const std::string& requestId) const {
    int level = 0;
    
    withExecutionContextRead(requestId, [&level](const ExecutionContext& context) {
        level = static_cast<int>(context.scriptStack.size());
    });
    
    return level;
}

void StateManager::setSubScriptResult(const std::string& requestId, const std::string& scriptName, 
                                     const nlohmann::json& result) {
    withExecutionContext(requestId, [&scriptName, &result](ExecutionContext& context) {
        context.subScriptResults[scriptName] = result;
    });
    
    SLOG_DEBUG().message("[STATE_MANAGER] Set sub-script result")
        .context("script_name", scriptName);
}

nlohmann::json StateManager::getSubScriptResult(const std::string& requestId, 
                                                const std::string& scriptName) const {
    nlohmann::json result;
    
    withExecutionContextRead(requestId, [&scriptName, &result](const ExecutionContext& context) {
        auto it = context.subScriptResults.find(scriptName);
        if (it != context.subScriptResults.end()) {
            result = it->second;
        }
    });
    
    return result;
}

void StateManager::logActivity(const std::string& activity) {
    std::stringstream ss;
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << " " << activity;
    m_activityLog.push(ss.str());
}

std::vector<std::string> StateManager::getRecentActivity() const {
    return m_activityLog.getRecent();
}

void StateManager::setMaxActivityLogSize(size_t maxSize) {
    m_activityLog.resize(maxSize);
    SLOG_DEBUG().message("[STATE_MANAGER] Activity log size set")
        .context("max_size", maxSize);
}

nlohmann::json StateManager::exportState() const {
    nlohmann::json state;
    
    // Export execution contexts
    {
        std::shared_lock<std::shared_mutex> lock(m_contextRwMutex);
        nlohmann::json contexts = nlohmann::json::array();
        
        for (const auto& pair : m_executionContexts) {
            if (!pair.second) continue;
            
            nlohmann::json contextJson;
            contextJson["requestId"] = pair.second->requestId;
            contextJson["originalRequest"] = pair.second->originalRequest;
            contextJson["status"] = static_cast<int>(pair.second->status);
            contextJson["errorMessage"] = pair.second->errorMessage;
            contextJson["scriptStack"] = pair.second->scriptStack;
            contextJson["variables"] = pair.second->variables->getAll();
            contextJson["subScriptResults"] = pair.second->subScriptResults;
            contextJson["commandIndex"] = pair.second->commandIndex;
            contextJson["shouldBreak"] = pair.second->shouldBreak;
            
            contexts.push_back(contextJson);
        }
        
        state["executionContexts"] = contexts;
    }
    
    // Export completed executions
    {
        std::shared_lock<std::shared_mutex> lock(m_resultRwMutex);
        nlohmann::json results = nlohmann::json::array();
        
        for (const auto& pair : m_completedExecutions) {
            nlohmann::json resultJson;
            resultJson["requestId"] = pair.first;
            resultJson["status"] = static_cast<int>(pair.second.status);
            resultJson["errorMessage"] = pair.second.errorMessage;
            
            results.push_back(resultJson);
        }
        
        state["completedExecutions"] = results;
    }
    
    // Export statistics
    state["statistics"]["totalRequests"] = m_stats.totalRequests.load();
    state["statistics"]["activeRequests"] = m_stats.activeRequests.load();
    state["statistics"]["completedRequests"] = m_stats.completedRequests.load();
    state["statistics"]["failedRequests"] = m_stats.failedRequests.load();
    state["statistics"]["variableAccesses"] = m_stats.variableAccesses.load();
    state["statistics"]["contextSwitches"] = m_stats.contextSwitches.load();
    
    // Export activity log
    state["activityLog"] = getRecentActivity();
    
    return state;
}

void StateManager::importState(const nlohmann::json& state) {
    // Clear existing state
    {
        std::unique_lock<std::shared_mutex> lock(m_contextRwMutex);
        m_executionContexts.clear();
    }
    {
        std::unique_lock<std::shared_mutex> lock(m_resultRwMutex);
        m_completedExecutions.clear();
    }
    
    // Import execution contexts
    if (state.contains("executionContexts") && state["executionContexts"].is_array()) {
        std::unique_lock<std::shared_mutex> lock(m_contextRwMutex);
        
        for (const auto& contextJson : state["executionContexts"]) {
            std::string requestId = contextJson["requestId"];
            std::string originalRequest = contextJson.value("originalRequest", "");
            
            ExecutionContext context(requestId, originalRequest);
            context.status = static_cast<ExecutionStatus>(contextJson.value("status", 0));
            context.errorMessage = contextJson.value("errorMessage", "");
            context.commandIndex = contextJson.value("commandIndex", 0);
            context.shouldBreak = contextJson.value("shouldBreak", false);
            
            if (contextJson.contains("scriptStack") && contextJson["scriptStack"].is_array()) {
                context.scriptStack = contextJson["scriptStack"].get<std::vector<std::string>>();
            }
            
            if (contextJson.contains("variables") && contextJson["variables"].is_object()) {
                for (const auto& [key, value] : contextJson["variables"].items()) {
                    context.variables->set(key, value);
                }
            }
            
            if (contextJson.contains("subScriptResults") && contextJson["subScriptResults"].is_object()) {
                context.subScriptResults = contextJson["subScriptResults"];
            }
            
            m_executionContexts[requestId] = std::make_unique<ExecutionContext>(std::move(context));
        }
    }
    
    // Import completed executions
    if (state.contains("completedExecutions") && state["completedExecutions"].is_array()) {
        std::unique_lock<std::shared_mutex> lock(m_resultRwMutex);
        
        for (const auto& resultJson : state["completedExecutions"]) {
            std::string requestId = resultJson["requestId"];
            TaskExecutionResult result;
            result.status = static_cast<ExecutionStatus>(resultJson.value("status", 0));
            result.errorMessage = resultJson.value("errorMessage", "");
            
            m_completedExecutions[requestId] = result;
        }
    }
    
    // Import statistics
    if (state.contains("statistics") && state["statistics"].is_object()) {
        const auto& stats = state["statistics"];
        m_stats.totalRequests = stats.value("totalRequests", 0);
        m_stats.activeRequests = stats.value("activeRequests", 0);
        m_stats.completedRequests = stats.value("completedRequests", 0);
        m_stats.failedRequests = stats.value("failedRequests", 0);
        m_stats.variableAccesses = stats.value("variableAccesses", 0);
        m_stats.contextSwitches = stats.value("contextSwitches", 0);
    }
    
    SLOG_INFO().message("[STATE_MANAGER] State imported successfully");
}

std::string StateManager::generateRequestId() {
    uint64_t id = m_requestCounter++;
    
    std::stringstream ss;
    ss << "REQ-" << std::hex << std::uppercase 
       << std::chrono::steady_clock::now().time_since_epoch().count()
       << "-" << id;
    
    return ss.str();
}

void StateManager::enforceCompletedExecutionsLimit() {
    // This method is called with m_resultRwMutex already locked
    if (m_completedExecutions.size() > m_maxCompletedExecutions) {
        // Find oldest execution
        std::string oldestId;
        std::chrono::steady_clock::time_point oldestTime = std::chrono::steady_clock::now();
        
        for (const auto& pair : m_completedExecutions) {
            auto it = m_executionContexts.find(pair.first);
            if (it != m_executionContexts.end() && it->second && it->second->endTime < oldestTime) {
                oldestTime = it->second->endTime;
                oldestId = pair.first;
            }
        }
        
        if (!oldestId.empty()) {
            m_completedExecutions.erase(oldestId);
        }
    }
}

ExecutionContext StateManager::createDefaultContext(const std::string& requestId, 
                                                   const std::string& userInput) {
    return ExecutionContext(requestId, userInput);
}

} // namespace burwell