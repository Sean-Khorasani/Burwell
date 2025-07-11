#include "feedback_controller.h"
#include "orchestrator.h"  // For ExecutionContext
#include "../environmental_perception/environmental_perception.h"
#include "../llm_connector/llm_connector.h"
#include "../common/structured_logger.h"
#include <algorithm>
#include <thread>

namespace burwell {

FeedbackController::FeedbackController()
    : m_environmentCheckIntervalMs(1000)
    , m_adaptationThresholdMs(2000)
    , m_continuousMonitoringEnabled(true)
    , m_maxEnvironmentHistorySize(100)
    , m_monitoringActive(false)
    , m_shouldStop(false) {
    SLOG_DEBUG().message("FeedbackController initialized");
}

FeedbackController::~FeedbackController() {
    stopContinuousMonitoring();
    SLOG_DEBUG().message("FeedbackController destroyed");
}

void FeedbackController::setEnvironmentalPerception(std::shared_ptr<EnvironmentalPerception> perception) {
    m_perception = perception;
}

void FeedbackController::setLLMConnector(std::shared_ptr<LLMConnector> llm) {
    m_llmConnector = llm;
}

void FeedbackController::setEnvironmentCheckIntervalMs(int intervalMs) {
    m_environmentCheckIntervalMs = intervalMs;
}

void FeedbackController::setAdaptationThresholdMs(int thresholdMs) {
    m_adaptationThresholdMs = thresholdMs;
}

void FeedbackController::setContinuousMonitoringEnabled(bool enabled) {
    m_continuousMonitoringEnabled = enabled;
    if (!enabled) {
        stopContinuousMonitoring();
    }
}

void FeedbackController::setMaxEnvironmentHistorySize(size_t maxSize) {
    m_maxEnvironmentHistorySize = maxSize;
    enforceHistoryLimit();
}

void FeedbackController::startContinuousMonitoring() {
    if (m_monitoringActive || !m_continuousMonitoringEnabled) {
        return;
    }
    
    m_shouldStop = false;
    m_monitoringActive = true;
    
    m_monitoringThread = std::thread(&FeedbackController::monitoringWorker, this);
    SLOG_INFO().message("Continuous environment monitoring started");
}

void FeedbackController::stopContinuousMonitoring() {
    if (!m_monitoringActive) {
        return;
    }
    
    m_shouldStop = true;
    
    if (m_monitoringThread.joinable()) {
        m_monitoringThread.join();
    }
    
    m_monitoringActive = false;
    SLOG_INFO().message("Continuous environment monitoring stopped");
}

bool FeedbackController::isMonitoringActive() const {
    return m_monitoringActive;
}

nlohmann::json FeedbackController::captureEnvironmentSnapshot() {
    if (!m_perception) {
        return nlohmann::json::object();
    }
    
    nlohmann::json snapshot;
    
    try {
        // Get window information
        auto windows = m_perception->getVisibleWindows();
        snapshot["windows"] = nlohmann::json::array();
        for (const auto& window : windows) {
            nlohmann::json windowJson = {
                {"title", window.title},
                {"className", window.className},
                {"isVisible", window.isVisible},
                {"isMinimized", window.isMinimized},
                {"isMaximized", window.isMaximized},
                {"position", {{"x", window.bounds.x}, {"y", window.bounds.y}}},
                {"size", {{"width", window.bounds.width}, {"height", window.bounds.height}}}
            };
            snapshot["windows"].push_back(windowJson);
        }
        
        // Get active window
        auto activeWindow = m_perception->getActiveWindow();
        if (!activeWindow.title.empty()) {
            snapshot["activeWindow"] = {
                {"title", activeWindow.title},
                {"className", activeWindow.className}
            };
        }
        
        // Get system info (if available)
        // This would include CPU usage, memory, etc. if implemented
        snapshot["system"] = {
            {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
        };
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Error capturing environment snapshot").context("error", e.what());
    }
    
    return snapshot;
}

bool FeedbackController::analyzeEnvironmentChanges(const nlohmann::json& currentEnv, const nlohmann::json& previousEnv) {
    if (currentEnv.empty() || previousEnv.empty()) {
        return false;
    }
    
    // Check for window changes
    if (currentEnv.contains("windows") && previousEnv.contains("windows")) {
        if (currentEnv["windows"].size() != previousEnv["windows"].size()) {
            return true;  // Number of windows changed
        }
        
        // Check active window change
        if (currentEnv.contains("activeWindow") && previousEnv.contains("activeWindow")) {
            if (currentEnv["activeWindow"]["title"] != previousEnv["activeWindow"]["title"]) {
                return true;  // Active window changed
            }
        }
    }
    
    // Calculate similarity
    double similarity = calculateEnvironmentSimilarity(currentEnv, previousEnv);
    return similarity < 0.9;  // Significant change if less than 90% similar
}

nlohmann::json FeedbackController::getEnvironmentDelta(const nlohmann::json& currentEnv, const nlohmann::json& previousEnv) {
    nlohmann::json delta;
    
    // Compare windows
    if (currentEnv.contains("windows") && previousEnv.contains("windows")) {
        delta["windowsAdded"] = nlohmann::json::array();
        delta["windowsRemoved"] = nlohmann::json::array();
        delta["windowsChanged"] = nlohmann::json::array();
        
        // Simple comparison - in production, would use window handles for better tracking
        auto currentWindows = currentEnv["windows"];
        auto previousWindows = previousEnv["windows"];
        
        // Check for new or changed windows
        for (const auto& window : currentWindows) {
            bool found = false;
            for (const auto& prevWindow : previousWindows) {
                if (window["title"] == prevWindow["title"] && 
                    window["className"] == prevWindow["className"]) {
                    found = true;
                    // Check if position or size changed
                    if (window["position"] != prevWindow["position"] ||
                        window["size"] != prevWindow["size"]) {
                        delta["windowsChanged"].push_back(window);
                    }
                    break;
                }
            }
            if (!found) {
                delta["windowsAdded"].push_back(window);
            }
        }
        
        // Check for removed windows
        for (const auto& prevWindow : previousWindows) {
            bool found = false;
            for (const auto& window : currentWindows) {
                if (window["title"] == prevWindow["title"] && 
                    window["className"] == prevWindow["className"]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                delta["windowsRemoved"].push_back(prevWindow);
            }
        }
    }
    
    // Check active window change
    if (currentEnv.contains("activeWindow") && previousEnv.contains("activeWindow")) {
        if (currentEnv["activeWindow"] != previousEnv["activeWindow"]) {
            delta["activeWindowChanged"] = {
                {"from", previousEnv["activeWindow"]},
                {"to", currentEnv["activeWindow"]}
            };
        }
    }
    
    return delta;
}

void FeedbackController::adaptExecutionPlan(ExecutionContext& context, const nlohmann::json& environmentChanges) {
    if (environmentChanges.empty()) {
        return;
    }
    
    SLOG_INFO().message("Adapting execution plan based on environment changes");
    
    // Apply adaptation rules
    auto applicableRules = evaluateAdaptationRules(environmentChanges);
    for (const auto& rule : applicableRules) {
        applyAdaptationRule(rule, context);
    }
    
    // Specific adaptations based on change type
    if (environmentChanges.contains("windowsRemoved") && !environmentChanges["windowsRemoved"].empty()) {
        adaptForWindowChange(context, environmentChanges["windowsRemoved"]);
    }
    
    if (environmentChanges.contains("activeWindowChanged")) {
        adaptForWindowChange(context, environmentChanges["activeWindowChanged"]);
    }
    
    // Request LLM analysis if significant changes
    if (isSignificantChange(environmentChanges)) {
        requestLLMEnvironmentAnalysis(context);
    }
}

nlohmann::json FeedbackController::generateAdaptiveCommandSuggestions(const ExecutionContext& context) {
    (void)context; // TODO: Use context for adaptive command suggestions
    nlohmann::json suggestions = nlohmann::json::array();
    
    // Get current environment
    auto currentEnv = captureEnvironmentSnapshot();
    
    // Analyze what commands might be affected
    auto affectedCommands = identifyAffectedCommands(getEnvironmentDelta(currentEnv, m_state.lastEnvironmentSnapshot));
    
    for (const auto& cmdType : affectedCommands) {
        // Suggest alternatives based on success rates
        double successRate = getCommandSuccessRate(cmdType);
        if (successRate < 0.5) {
            // Low success rate, suggest alternatives
            suggestions.push_back({
                {"command", cmdType},
                {"successRate", successRate},
                {"suggestion", "Consider using alternative approach"},
                {"alternatives", generateAlternativesForCommand(cmdType)}
            });
        }
    }
    
    return suggestions;
}

void FeedbackController::requestLLMEnvironmentAnalysis(ExecutionContext& context) {
    if (!m_llmConnector) {
        return;
    }
    
    SLOG_INFO().message("Requesting LLM environment analysis");
    
    // Build analysis request
    nlohmann::json analysisRequest = {
        {"type", "environment_analysis"},
        {"currentEnvironment", captureEnvironmentSnapshot()},
        {"executionContext", {
            {"currentTask", context.originalRequest},
            {"variables", context.variables}
        }},
        {"environmentHistory", getEnvironmentHistory()}
    };
    
    // Send to LLM for analysis
    std::string prompt = "Analyze the environment changes and suggest execution plan adaptations:\n" + 
                        analysisRequest.dump(2);
    
    try {
        auto llmResponse = m_llmConnector->sendPrompt(prompt);
        
        // Process LLM suggestions
        if (llmResponse.contains("suggestions")) {
            context.variables["llm_adaptation_suggestions"] = llmResponse["suggestions"];
        }
    } catch (const std::exception& e) {
        SLOG_ERROR().message("LLM environment analysis failed").context("error", e.what());
    }
}

void FeedbackController::updateCommandSuccessRate(const std::string& command, bool success) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    
    if (success) {
        m_state.commandSuccessCounts[command]++;
    } else {
        m_state.commandFailureCounts[command]++;
    }
}

double FeedbackController::getCommandSuccessRate(const std::string& command) const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    
    auto successIt = m_state.commandSuccessCounts.find(command);
    auto failureIt = m_state.commandFailureCounts.find(command);
    
    int successes = (successIt != m_state.commandSuccessCounts.end()) ? successIt->second : 0;
    int failures = (failureIt != m_state.commandFailureCounts.end()) ? failureIt->second : 0;
    
    int total = successes + failures;
    if (total == 0) {
        return 1.0;  // No data, assume success
    }
    
    return static_cast<double>(successes) / total;
}

nlohmann::json FeedbackController::getSuccessMetrics() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    
    nlohmann::json metrics;
    
    for (const auto& [command, count] : m_state.commandSuccessCounts) {
        double successRate = getCommandSuccessRate(command);
        metrics[command] = {
            {"successCount", count},
            {"failureCount", m_state.commandFailureCounts.count(command) ? m_state.commandFailureCounts.at(command) : 0},
            {"successRate", successRate}
        };
    }
    
    return metrics;
}

void FeedbackController::resetSuccessMetrics() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_state.commandSuccessCounts.clear();
    m_state.commandFailureCounts.clear();
}

std::vector<nlohmann::json> FeedbackController::getEnvironmentHistory() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_state.environmentHistory;
}

nlohmann::json FeedbackController::getLastEnvironmentSnapshot() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_state.lastEnvironmentSnapshot;
}

void FeedbackController::clearEnvironmentHistory() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_state.environmentHistory.clear();
}

void FeedbackController::addAdaptationRule(const AdaptationRule& rule) {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    m_adaptationRules.push_back(rule);
}

void FeedbackController::removeAdaptationRule(const std::string& ruleName) {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    m_adaptationRules.erase(
        std::remove_if(m_adaptationRules.begin(), m_adaptationRules.end(),
                      [&ruleName](const AdaptationRule& rule) { return rule.name == ruleName; }),
        m_adaptationRules.end()
    );
}

std::vector<FeedbackController::AdaptationRule> FeedbackController::getAdaptationRules() const {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    return m_adaptationRules;
}

void FeedbackController::setAdaptationRuleEnabled(const std::string& ruleName, bool enabled) {
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    for (auto& rule : m_adaptationRules) {
        if (rule.name == ruleName) {
            rule.enabled = enabled;
            break;
        }
    }
}

// Private methods

void FeedbackController::monitoringWorker() {
    while (!m_shouldStop && m_continuousMonitoringEnabled) {
        auto startTime = std::chrono::steady_clock::now();
        
        // Capture environment
        auto currentEnv = captureEnvironmentSnapshot();
        
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            
            // Check for changes
            if (!m_state.lastEnvironmentSnapshot.empty()) {
                auto delta = getEnvironmentDelta(currentEnv, m_state.lastEnvironmentSnapshot);
                if (!delta.empty() && isSignificantChange(delta)) {
                    processEnvironmentChange(delta);
                }
            }
            
            // Update state
            m_state.lastEnvironmentSnapshot = currentEnv;
            m_state.lastEnvironmentCheck = std::chrono::steady_clock::now();
            m_state.environmentHistory.push_back(currentEnv);
            enforceHistoryLimit();
        }
        
        // Sleep for the remaining interval
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        auto sleepTime = std::chrono::milliseconds(m_environmentCheckIntervalMs) - elapsed;
        if (sleepTime.count() > 0) {
            std::this_thread::sleep_for(sleepTime);
        }
    }
}

void FeedbackController::processEnvironmentChange(const nlohmann::json& environmentDelta) {
    SLOG_INFO().message("Processing environment change").context("environment_delta", environmentDelta.dump());
    
    // Evaluate adaptation rules
    auto applicableRules = evaluateAdaptationRules(environmentDelta);
    
    // Log summary
    if (!applicableRules.empty()) {
        std::string summary = generateAdaptationSummary(applicableRules);
        SLOG_INFO().message("Adaptation summary").context("summary", summary);
    }
}

std::vector<FeedbackController::AdaptationRule> FeedbackController::evaluateAdaptationRules(const nlohmann::json& environmentDelta) {
    std::vector<AdaptationRule> applicableRules;
    
    std::lock_guard<std::mutex> lock(m_rulesMutex);
    for (const auto& rule : m_adaptationRules) {
        if (!rule.enabled) {
            continue;
        }
        
        // Simple condition evaluation - in production, would use a proper expression evaluator
        bool conditionMet = false;
        
        if (rule.condition == "window_closed" && environmentDelta.contains("windowsRemoved")) {
            conditionMet = !environmentDelta["windowsRemoved"].empty();
        } else if (rule.condition == "window_changed" && environmentDelta.contains("activeWindowChanged")) {
            conditionMet = true;
        } else if (rule.condition == "windows_added" && environmentDelta.contains("windowsAdded")) {
            conditionMet = !environmentDelta["windowsAdded"].empty();
        }
        
        if (conditionMet) {
            applicableRules.push_back(rule);
        }
    }
    
    // Sort by priority
    std::sort(applicableRules.begin(), applicableRules.end(),
              [](const AdaptationRule& a, const AdaptationRule& b) {
                  return a.priority > b.priority;
              });
    
    return applicableRules;
}

void FeedbackController::applyAdaptationRule(const AdaptationRule& rule, ExecutionContext& context) {
    SLOG_INFO().message("Applying adaptation rule").context("rule_name", rule.name);
    
    if (rule.action == "retry_command") {
        context.variables["retry_required"] = true;
        context.variables["retry_reason"] = "Environment change detected";
    } else if (rule.action == "wait_and_retry") {
        context.variables["wait_required"] = true;
        context.variables["wait_duration_ms"] = rule.parameters.value("wait_ms", 1000);
    } else if (rule.action == "find_alternative_window") {
        context.variables["find_alternative"] = true;
        context.variables["alternative_type"] = "window";
    }
}

bool FeedbackController::isSignificantChange(const nlohmann::json& delta) const {
    // Check if any major changes occurred
    if (delta.contains("windowsRemoved") && !delta["windowsRemoved"].empty()) {
        return true;
    }
    
    if (delta.contains("activeWindowChanged")) {
        return true;
    }
    
    if (delta.contains("windowsAdded") && delta["windowsAdded"].size() > 2) {
        return true;  // Many new windows
    }
    
    return false;
}

double FeedbackController::calculateEnvironmentSimilarity(const nlohmann::json& env1, const nlohmann::json& env2) const {
    if (env1.empty() || env2.empty()) {
        return 0.0;
    }
    
    // Simple similarity based on window count and active window
    double similarity = 1.0;
    
    if (env1.contains("windows") && env2.contains("windows")) {
        int count1 = env1["windows"].size();
        int count2 = env2["windows"].size();
        int diff = std::abs(count1 - count2);
        similarity -= (diff * 0.1);  // 10% penalty per window difference
    }
    
    if (env1.contains("activeWindow") && env2.contains("activeWindow")) {
        if (env1["activeWindow"]["title"] != env2["activeWindow"]["title"]) {
            similarity -= 0.3;  // 30% penalty for different active window
        }
    }
    
    return std::max(0.0, similarity);
}

std::vector<std::string> FeedbackController::identifyAffectedCommands(const nlohmann::json& environmentDelta) const {
    std::vector<std::string> affected;
    
    if (environmentDelta.contains("windowsRemoved") || environmentDelta.contains("activeWindowChanged")) {
        // Window-related commands might be affected
        affected.push_back("WINDOW_FOCUS");
        affected.push_back("WINDOW_CLICK");
        affected.push_back("UIA_FOCUS_WINDOW");
    }
    
    if (environmentDelta.contains("windowsAdded")) {
        // New windows might interfere
        affected.push_back("MOUSE_CLICK");
        affected.push_back("KEY_PRESS");
    }
    
    return affected;
}

void FeedbackController::adaptForWindowChange(ExecutionContext& context, const nlohmann::json& windowDelta) {
    SLOG_INFO().message("Adapting for window change");
    
    // Set flags for the execution engine to handle
    context.variables["window_change_detected"] = true;
    context.variables["window_delta"] = windowDelta;
    
    // Suggest waiting or retrying
    context.variables["suggested_action"] = "wait_and_retry";
}

void FeedbackController::adaptForApplicationChange(ExecutionContext& context, const nlohmann::json& appDelta) {
    SLOG_INFO().message("Adapting for application change");
    context.variables["application_change_detected"] = true;
    context.variables["app_delta"] = appDelta;
}

void FeedbackController::adaptForResourceChange(ExecutionContext& context, const nlohmann::json& resourceDelta) {
    SLOG_INFO().message("Adapting for resource change");
    context.variables["resource_change_detected"] = true;
    context.variables["resource_delta"] = resourceDelta;
}

void FeedbackController::adaptForErrorCondition(ExecutionContext& context, const nlohmann::json& errorInfo) {
    SLOG_INFO().message("Adapting for error condition");
    context.variables["error_adaptation_required"] = true;
    context.variables["error_info"] = errorInfo;
}

void FeedbackController::enforceHistoryLimit() {
    // Should be called with m_stateMutex already locked
    while (m_state.environmentHistory.size() > m_maxEnvironmentHistorySize) {
        m_state.environmentHistory.erase(m_state.environmentHistory.begin());
    }
}

std::string FeedbackController::generateAdaptationSummary(const std::vector<AdaptationRule>& appliedRules) const {
    std::stringstream ss;
    ss << "Applied " << appliedRules.size() << " adaptation rules: ";
    
    for (size_t i = 0; i < appliedRules.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << appliedRules[i].name << " (" << appliedRules[i].action << ")";
    }
    
    return ss.str();
}

nlohmann::json FeedbackController::generateAlternativesForCommand(const std::string& cmdType) const {
    nlohmann::json alternatives = nlohmann::json::array();
    
    // Simple alternatives based on command type
    if (cmdType == "WINDOW_FOCUS") {
        alternatives.push_back({
            {"command", "UIA_ENUM_WINDOWS"},
            {"description", "Enumerate windows to find the target"}
        });
        alternatives.push_back({
            {"command", "WINDOW_FIND_BY_TITLE"},
            {"description", "Find window by partial title match"}
        });
    } else if (cmdType == "MOUSE_CLICK") {
        alternatives.push_back({
            {"command", "UIA_MOUSE_CLICK"},
            {"description", "Use UIA mouse click instead"}
        });
        alternatives.push_back({
            {"command", "KEY_TAB"},
            {"description", "Navigate using keyboard instead"}
        });
    }
    
    return alternatives;
}

} // namespace burwell