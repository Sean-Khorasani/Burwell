#include "state_manager.h"
#include "orchestrator.h"  // For ExecutionContext
#include "../common/structured_logger.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <random>

namespace burwell {

StateManager::StateManager()
    : m_maxCompletedExecutions(100)
    , m_maxActivityLogSize(1000) {
    SLOG_DEBUG().message("StateManager initialized");
}

StateManager::~StateManager() {
    SLOG_DEBUG().message("StateManager destroyed");
}

void StateManager::setMaxCompletedExecutions(size_t maxExecutions) {
    m_maxCompletedExecutions = maxExecutions;
    enforceCompletedExecutionsLimit();
}

std::string StateManager::createRequest(const std::string& userInput) {
    std::string requestId = generateRequestId();
    createExecutionContext(requestId, userInput);
    return requestId;
}

bool StateManager::hasRequest(const std::string& requestId) const {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    return m_executionContexts.find(requestId) != m_executionContexts.end();
}

void StateManager::removeRequest(const std::string& requestId) {
    {
        std::lock_guard<std::mutex> lock(m_contextMutex);
        m_executionContexts.erase(requestId);
    }
    
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_completedExecutions.erase(requestId);
    }
}

ExecutionContext& StateManager::getExecutionContext(const std::string& requestId) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    auto it = m_executionContexts.find(requestId);
    if (it == m_executionContexts.end()) {
        // Create a default context if not found
        createExecutionContext(requestId, "");
        it = m_executionContexts.find(requestId);
    }
    return it->second;
}

const ExecutionContext& StateManager::getExecutionContext(const std::string& requestId) const {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    auto it = m_executionContexts.find(requestId);
    if (it == m_executionContexts.end()) {
        static ExecutionContext emptyContext;
        return emptyContext;
    }
    return it->second;
}

void StateManager::createExecutionContext(const std::string& requestId, const std::string& userInput) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    m_executionContexts[requestId] = createDefaultContext(requestId, userInput);
}

void StateManager::updateExecutionContext(const std::string& requestId, const ExecutionContext& context) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    m_executionContexts[requestId] = context;
}

void StateManager::markExecutionActive(const std::string& requestId) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    if (m_executionContexts.find(requestId) != m_executionContexts.end()) {
        logActivity("Execution started: " + requestId);
    }
}

void StateManager::markExecutionComplete(const std::string& requestId, const TaskExecutionResult& result) {
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_completedExecutions[requestId] = result;
        enforceCompletedExecutionsLimit();
    }
    
    std::string status = result.success ? "succeeded" : "failed";
    logActivity("Execution " + status + ": " + requestId);
}

bool StateManager::isExecutionActive(const std::string& requestId) const {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    return m_executionContexts.find(requestId) != m_executionContexts.end() &&
           !hasExecutionResult(requestId);
}

std::vector<std::string> StateManager::getActiveRequests() const {
    std::vector<std::string> activeRequests;
    
    std::lock_guard<std::mutex> lock(m_contextMutex);
    for (const auto& [requestId, context] : m_executionContexts) {
        if (!hasExecutionResult(requestId)) {
            activeRequests.push_back(requestId);
        }
    }
    
    return activeRequests;
}

size_t StateManager::getActiveExecutionCount() const {
    return getActiveRequests().size();
}

TaskExecutionResult StateManager::getExecutionResult(const std::string& requestId) const {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    auto it = m_completedExecutions.find(requestId);
    if (it != m_completedExecutions.end()) {
        return it->second;
    }
    
    TaskExecutionResult emptyResult;
    emptyResult.executionId = requestId;
    emptyResult.success = false;
    emptyResult.errorMessage = "Execution result not found";
    return emptyResult;
}

bool StateManager::hasExecutionResult(const std::string& requestId) const {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    return m_completedExecutions.find(requestId) != m_completedExecutions.end();
}

std::vector<std::string> StateManager::getCompletedRequests() const {
    std::vector<std::string> completedRequests;
    
    std::lock_guard<std::mutex> lock(m_resultMutex);
    for (const auto& [requestId, result] : m_completedExecutions) {
        completedRequests.push_back(requestId);
    }
    
    return completedRequests;
}

void StateManager::cleanupCompletedExecutions() {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    
    // Remove oldest executions if we exceed the limit
    while (m_completedExecutions.size() > m_maxCompletedExecutions / 2) {
        // Find the oldest execution (simple approach - could be optimized with timestamps)
        if (!m_completedExecutions.empty()) {
            m_completedExecutions.erase(m_completedExecutions.begin());
        }
    }
}

void StateManager::setVariable(const std::string& requestId, const std::string& name, const nlohmann::json& value) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    auto it = m_executionContexts.find(requestId);
    if (it != m_executionContexts.end()) {
        it->second.variables[name] = value;
    }
}

nlohmann::json StateManager::getVariable(const std::string& requestId, const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    auto it = m_executionContexts.find(requestId);
    if (it != m_executionContexts.end()) {
        auto varIt = it->second.variables.find(name);
        if (varIt != it->second.variables.end()) {
            return varIt->second;
        }
    }
    return nlohmann::json();
}

bool StateManager::hasVariable(const std::string& requestId, const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    auto it = m_executionContexts.find(requestId);
    if (it != m_executionContexts.end()) {
        return it->second.variables.find(name) != it->second.variables.end();
    }
    return false;
}

void StateManager::inheritVariables(const std::string& fromRequestId, const std::string& toRequestId) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    
    auto fromIt = m_executionContexts.find(fromRequestId);
    auto toIt = m_executionContexts.find(toRequestId);
    
    if (fromIt != m_executionContexts.end() && toIt != m_executionContexts.end()) {
        for (const auto& [name, value] : fromIt->second.variables) {
            toIt->second.variables[name] = value;
        }
    }
}

void StateManager::pushScript(const std::string& requestId, const std::string& scriptPath) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    auto it = m_executionContexts.find(requestId);
    if (it != m_executionContexts.end()) {
        it->second.scriptStack.push_back(scriptPath);
        it->second.nestingLevel++;
    }
}

void StateManager::popScript(const std::string& requestId) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    auto it = m_executionContexts.find(requestId);
    if (it != m_executionContexts.end() && !it->second.scriptStack.empty()) {
        it->second.scriptStack.pop_back();
        it->second.nestingLevel--;
    }
}

bool StateManager::isScriptInStack(const std::string& requestId, const std::string& scriptPath) const {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    auto it = m_executionContexts.find(requestId);
    if (it != m_executionContexts.end()) {
        const auto& stack = it->second.scriptStack;
        return std::find(stack.begin(), stack.end(), scriptPath) != stack.end();
    }
    return false;
}

int StateManager::getScriptNestingLevel(const std::string& requestId) const {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    auto it = m_executionContexts.find(requestId);
    if (it != m_executionContexts.end()) {
        return it->second.nestingLevel;
    }
    return 0;
}

void StateManager::setSubScriptResult(const std::string& requestId, const std::string& scriptName, const nlohmann::json& result) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    auto it = m_executionContexts.find(requestId);
    if (it != m_executionContexts.end()) {
        it->second.subScriptResults[scriptName] = result;
    }
}

nlohmann::json StateManager::getSubScriptResult(const std::string& requestId, const std::string& scriptName) const {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    auto it = m_executionContexts.find(requestId);
    if (it != m_executionContexts.end()) {
        auto resIt = it->second.subScriptResults.find(scriptName);
        if (resIt != it->second.subScriptResults.end()) {
            return resIt->second;
        }
    }
    return nlohmann::json();
}

void StateManager::logActivity(const std::string& activity) {
    std::lock_guard<std::mutex> lock(m_activityMutex);
    
    // Add timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << " - " << activity;
    
    m_recentActivity.push_back(ss.str());
    enforceActivityLogLimit();
}

std::vector<std::string> StateManager::getRecentActivity() const {
    std::lock_guard<std::mutex> lock(m_activityMutex);
    return m_recentActivity;
}

void StateManager::setMaxActivityLogSize(size_t maxSize) {
    m_maxActivityLogSize = maxSize;
    enforceActivityLogLimit();
}

nlohmann::json StateManager::exportState() const {
    nlohmann::json state;
    
    {
        std::lock_guard<std::mutex> lock(m_contextMutex);
        for (const auto& [id, context] : m_executionContexts) {
            state["contexts"][id] = {
                {"requestId", context.requestId},
                {"originalRequest", context.originalRequest},
                {"variables", context.variables},
                {"nestingLevel", context.nestingLevel},
                {"scriptStack", context.scriptStack}
            };
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        for (const auto& [id, result] : m_completedExecutions) {
            state["results"][id] = {
                {"success", result.success},
                {"errorMessage", result.errorMessage},
                {"output", result.output}
            };
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(m_activityMutex);
        state["recentActivity"] = m_recentActivity;
    }
    
    return state;
}

void StateManager::importState(const nlohmann::json& state) {
    // Clear existing state
    {
        std::lock_guard<std::mutex> lock(m_contextMutex);
        m_executionContexts.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_completedExecutions.clear();
    }
    
    // Import contexts
    if (state.contains("contexts")) {
        std::lock_guard<std::mutex> lock(m_contextMutex);
        for (auto& [id, contextData] : state["contexts"].items()) {
            ExecutionContext context;
            context.requestId = contextData.value("requestId", "");
            context.originalRequest = contextData.value("originalRequest", "");
            context.variables = contextData.value("variables", nlohmann::json::object());
            context.nestingLevel = contextData.value("nestingLevel", 0);
            
            if (contextData.contains("scriptStack")) {
                for (const auto& script : contextData["scriptStack"]) {
                    context.scriptStack.push_back(script);
                }
            }
            
            m_executionContexts[id] = context;
        }
    }
    
    // Import results
    if (state.contains("results")) {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        for (auto& [id, resultData] : state["results"].items()) {
            TaskExecutionResult result;
            result.executionId = id;
            result.success = resultData.value("success", false);
            result.errorMessage = resultData.value("errorMessage", "");
            result.output = resultData.value("output", "");
            m_completedExecutions[id] = result;
        }
    }
    
    // Import activity log
    if (state.contains("recentActivity")) {
        std::lock_guard<std::mutex> lock(m_activityMutex);
        m_recentActivity.clear();
        for (const auto& activity : state["recentActivity"]) {
            m_recentActivity.push_back(activity);
        }
    }
}

// Private methods

std::string StateManager::generateRequestId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char* hexChars = "0123456789ABCDEF";
    
    std::stringstream ss;
    ss << "REQ-";
    
    // Add timestamp part
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d%H%M%S");
    ss << "-";
    
    // Add random part
    for (int i = 0; i < 8; ++i) {
        ss << hexChars[dis(gen)];
    }
    
    return ss.str();
}

void StateManager::enforceCompletedExecutionsLimit() {
    // Already under lock when called
    while (m_completedExecutions.size() > m_maxCompletedExecutions) {
        m_completedExecutions.erase(m_completedExecutions.begin());
    }
}

void StateManager::enforceActivityLogLimit() {
    // Already under lock when called
    while (m_recentActivity.size() > m_maxActivityLogSize) {
        m_recentActivity.erase(m_recentActivity.begin());
    }
}

ExecutionContext StateManager::createDefaultContext(const std::string& requestId, const std::string& userInput) {
    ExecutionContext context;
    context.requestId = requestId;
    context.originalRequest = userInput;
    context.requiresUserConfirmation = false;
    context.nestingLevel = 0;
    context.maxNestingLevel = 3;  // Default max nesting
    return context;
}

} // namespace burwell