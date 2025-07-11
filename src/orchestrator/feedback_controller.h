#ifndef BURWELL_FEEDBACK_CONTROLLER_H
#define BURWELL_FEEDBACK_CONTROLLER_H

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "../common/types.h"

namespace burwell {

// Forward declarations
class EnvironmentalPerception;
class LLMConnector;
struct ExecutionContext;

/**
 * @class FeedbackController
 * @brief Manages continuous environment monitoring and adaptive execution
 * 
 * This class implements the intelligent feedback loop system that monitors
 * environmental changes and adapts execution plans accordingly.
 */
class FeedbackController {
public:
    FeedbackController();
    ~FeedbackController();

    // Dependencies
    void setEnvironmentalPerception(std::shared_ptr<EnvironmentalPerception> perception);
    void setLLMConnector(std::shared_ptr<LLMConnector> llm);

    // Configuration
    void setEnvironmentCheckIntervalMs(int intervalMs);
    void setAdaptationThresholdMs(int thresholdMs);
    void setContinuousMonitoringEnabled(bool enabled);
    void setMaxEnvironmentHistorySize(size_t maxSize);

    // Monitoring control
    void startContinuousMonitoring();
    void stopContinuousMonitoring();
    bool isMonitoringActive() const;

    // Environment analysis
    nlohmann::json captureEnvironmentSnapshot();
    bool analyzeEnvironmentChanges(const nlohmann::json& currentEnv, const nlohmann::json& previousEnv);
    nlohmann::json getEnvironmentDelta(const nlohmann::json& currentEnv, const nlohmann::json& previousEnv);

    // Adaptation
    void adaptExecutionPlan(ExecutionContext& context, const nlohmann::json& environmentChanges);
    nlohmann::json generateAdaptiveCommandSuggestions(const ExecutionContext& context);
    void requestLLMEnvironmentAnalysis(ExecutionContext& context);

    // Success metrics
    void updateCommandSuccessRate(const std::string& command, bool success);
    double getCommandSuccessRate(const std::string& command) const;
    nlohmann::json getSuccessMetrics() const;
    void resetSuccessMetrics();

    // Environment history
    std::vector<nlohmann::json> getEnvironmentHistory() const;
    nlohmann::json getLastEnvironmentSnapshot() const;
    void clearEnvironmentHistory();

    // Adaptation rules
    struct AdaptationRule {
        std::string name;
        std::string condition;  // JSON path expression
        std::string action;     // Adaptation action type
        nlohmann::json parameters;
        int priority;
        bool enabled;
    };

    void addAdaptationRule(const AdaptationRule& rule);
    void removeAdaptationRule(const std::string& ruleName);
    std::vector<AdaptationRule> getAdaptationRules() const;
    void setAdaptationRuleEnabled(const std::string& ruleName, bool enabled);

private:
    // Dependencies
    std::shared_ptr<EnvironmentalPerception> m_perception;
    std::shared_ptr<LLMConnector> m_llmConnector;

    // Configuration
    int m_environmentCheckIntervalMs;
    int m_adaptationThresholdMs;
    bool m_continuousMonitoringEnabled;
    size_t m_maxEnvironmentHistorySize;

    // State
    struct FeedbackLoopState {
        nlohmann::json lastEnvironmentSnapshot;
        std::chrono::steady_clock::time_point lastEnvironmentCheck;
        nlohmann::json currentExecutionPlan;
        std::vector<nlohmann::json> environmentHistory;
        std::map<std::string, int> commandSuccessCounts;
        std::map<std::string, int> commandFailureCounts;
    };

    FeedbackLoopState m_state;
    mutable std::mutex m_stateMutex;

    // Monitoring thread
    std::thread m_monitoringThread;
    std::atomic<bool> m_monitoringActive;
    std::atomic<bool> m_shouldStop;

    // Adaptation rules
    std::vector<AdaptationRule> m_adaptationRules;
    mutable std::mutex m_rulesMutex;

    // Worker methods
    void monitoringWorker();
    void processEnvironmentChange(const nlohmann::json& environmentDelta);
    std::vector<AdaptationRule> evaluateAdaptationRules(const nlohmann::json& environmentDelta);
    void applyAdaptationRule(const AdaptationRule& rule, ExecutionContext& context);

    // Analysis helpers
    bool isSignificantChange(const nlohmann::json& delta) const;
    double calculateEnvironmentSimilarity(const nlohmann::json& env1, const nlohmann::json& env2) const;
    std::vector<std::string> identifyAffectedCommands(const nlohmann::json& environmentDelta) const;

    // Adaptation strategies
    void adaptForWindowChange(ExecutionContext& context, const nlohmann::json& windowDelta);
    void adaptForApplicationChange(ExecutionContext& context, const nlohmann::json& appDelta);
    void adaptForResourceChange(ExecutionContext& context, const nlohmann::json& resourceDelta);
    void adaptForErrorCondition(ExecutionContext& context, const nlohmann::json& errorInfo);

    // Utility methods
    void enforceHistoryLimit();
    std::string generateAdaptationSummary(const std::vector<AdaptationRule>& appliedRules) const;
    nlohmann::json generateAlternativesForCommand(const std::string& cmdType) const;
};

} // namespace burwell

#endif // BURWELL_FEEDBACK_CONTROLLER_H