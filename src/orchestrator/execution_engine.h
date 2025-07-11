#ifndef BURWELL_EXECUTION_ENGINE_H
#define BURWELL_EXECUTION_ENGINE_H

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "../common/types.h"

namespace burwell {

// Forward declarations
class OCAL;
class EnvironmentalPerception;
class UIModule;
struct ExecutionContext;

/**
 * @class ExecutionEngine
 * @brief Handles the actual execution of commands and command sequences
 * 
 * This class is responsible for executing individual commands and sequences
 * of commands. It was extracted from the Orchestrator to reduce complexity.
 */
class ExecutionEngine {
public:
    ExecutionEngine();
    ~ExecutionEngine();

    // Component dependencies
    void setOCAL(std::shared_ptr<OCAL> ocal);
    void setEnvironmentalPerception(std::shared_ptr<EnvironmentalPerception> perception);
    void setUIModule(std::shared_ptr<UIModule> ui);

    // Configuration
    void setCommandSequenceDelayMs(int delayMs);
    void setExecutionTimeoutMs(int timeoutMs);
    void setConfirmationRequired(bool required);

    // Main execution methods
    TaskExecutionResult executeCommandSequence(const nlohmann::json& commands, ExecutionContext& context);
    TaskExecutionResult executeCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeScriptFile(const std::string& scriptPath, ExecutionContext& context);

    // Variable substitution
    std::string substituteVariables(const std::string& input, const ExecutionContext& context);
    nlohmann::json substituteVariablesInParams(const nlohmann::json& params, const ExecutionContext& context);

    // Validation
    bool isCommandSafe(const nlohmann::json& command);
    bool requiresUserConfirmation(const nlohmann::json& command);

    // Loop control
    TaskExecutionResult executeWhileLoop(const nlohmann::json& command, ExecutionContext& context);
    bool evaluateLoopCondition(const nlohmann::json& condition, ExecutionContext& context);

private:
    // Component dependencies
    std::shared_ptr<OCAL> m_ocal;
    std::shared_ptr<EnvironmentalPerception> m_perception;
    std::shared_ptr<UIModule> m_ui;

    // Configuration
    int m_commandSequenceDelayMs;
    int m_executionTimeoutMs;
    bool m_confirmationRequired;

    // Command type handlers
    TaskExecutionResult executeMouseCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeKeyboardCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeApplicationCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeSystemCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeWindowCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeWaitCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeControlCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeScriptCommand(const nlohmann::json& command, ExecutionContext& context);
    TaskExecutionResult executeUiaCommand(const nlohmann::json& command, ExecutionContext& context);

    // Helper methods
    bool validateCommandParameters(const nlohmann::json& command);
    void updateExecutionLog(ExecutionContext& context, const std::string& entry);
    std::string formatCommandDescription(const nlohmann::json& command);
    bool checkTimeout(double startTime, int timeoutMs);
    
    // Variable evaluation
    std::string evaluateVariableExpression(const std::string& expression, const ExecutionContext& context);
    bool evaluateConditionExpression(const std::string& expression, const ExecutionContext& context);
};

} // namespace burwell

#endif // BURWELL_EXECUTION_ENGINE_H