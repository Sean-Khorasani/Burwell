#include "execution_engine.h"
#include "orchestrator.h"  // For ExecutionContext
#include "../common/structured_logger.h"
#include "../common/input_validator.h"
#include "../common/file_utils.h"
#include "../common/shutdown_manager.h"
#include "../ocal/ocal.h"
#include "../ocal/window_operations.h"
#include "../ocal/input_operations.h"
#include "../environmental_perception/environmental_perception.h"
#include "../ui_module/ui_module.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <regex>
#include <sstream>
#include <atomic>

namespace burwell {

ExecutionEngine::ExecutionEngine()
    : m_commandSequenceDelayMs(1000)
    , m_executionTimeoutMs(30000)
    , m_confirmationRequired(true) {
    SLOG_DEBUG().message("ExecutionEngine initialized");
}

ExecutionEngine::~ExecutionEngine() {
    SLOG_DEBUG().message("ExecutionEngine destroyed");
}

void ExecutionEngine::setOCAL(std::shared_ptr<OCAL> ocal) {
    m_ocal = ocal;
}

void ExecutionEngine::setEnvironmentalPerception(std::shared_ptr<EnvironmentalPerception> perception) {
    m_perception = perception;
}

void ExecutionEngine::setUIModule(std::shared_ptr<UIModule> ui) {
    m_ui = ui;
}

void ExecutionEngine::setCommandSequenceDelayMs(int delayMs) {
    m_commandSequenceDelayMs = delayMs;
}

void ExecutionEngine::setExecutionTimeoutMs(int timeoutMs) {
    m_executionTimeoutMs = timeoutMs;
}

void ExecutionEngine::setConfirmationRequired(bool required) {
    m_confirmationRequired = required;
}

TaskExecutionResult ExecutionEngine::executeCommandSequence(const nlohmann::json& commands, ExecutionContext& context) {
    TaskExecutionResult result;
    result.success = false;
    result.executionId = context.requestId;
    
    if (!commands.is_array()) {
        result.errorMessage = "Commands must be an array";
        return result;
    }
    
    double startTime = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    for (size_t i = 0; i < commands.size(); ++i) {
        const auto& command = commands[i];
        
        // Check for shutdown request
        if (ShutdownManager::getInstance().isShutdownRequested()) {
            result.errorMessage = "Execution cancelled by user";
            result.status = ExecutionStatus::CANCELLED;
            break;
        }
        
        // Check timeout
        if (checkTimeout(startTime, m_executionTimeoutMs)) {
            result.errorMessage = "Execution timeout exceeded";
            result.status = ExecutionStatus::FAILED;
            break;
        }
        
        // Execute individual command
        auto cmdResult = executeCommand(command, context);
        
        if (!cmdResult.success) {
            result = cmdResult;
            result.errorMessage = "Command " + std::to_string(i + 1) + " failed: " + cmdResult.errorMessage;
            break;
        }
        
        // Add command result to overall output
        if (!cmdResult.output.empty()) {
            if (!result.output.empty()) {
                result.output += "\n";
            }
            result.output += cmdResult.output;
        }
        
        // Delay between commands if configured
        if (m_commandSequenceDelayMs > 0 && i < commands.size() - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_commandSequenceDelayMs));
        }
    }
    
    // Calculate execution time
    double endTime = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    result.executionTimeMs = endTime - startTime;
    
    if (result.errorMessage.empty()) {
        result.success = true;
        result.status = ExecutionStatus::COMPLETED;
    }
    
    return result;
}

TaskExecutionResult ExecutionEngine::executeCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    result.success = false;
    
    try {
        // Validate command structure
        if (!command.contains("command") || !command["command"].is_string()) {
            result.errorMessage = "Invalid command structure: missing 'command' field";
            return result;
        }
        
        std::string commandType = command["command"];
        
        // Log command execution
        std::string logEntry = "Executing command: " + commandType;
        if (command.contains("description")) {
            logEntry += " - " + command["description"].get<std::string>();
        }
        updateExecutionLog(context, logEntry);
        SLOG_INFO().message(logEntry);
        
        // Check if command requires confirmation
        if (m_confirmationRequired && requiresUserConfirmation(command)) {
            if (m_ui) {
                std::string prompt = "Execute command: " + formatCommandDescription(command) + "?";
                if (!m_ui->promptUser(prompt)) {
                    result.errorMessage = "User cancelled command execution";
                    result.status = ExecutionStatus::CANCELLED;
                    return result;
                }
            }
        }
        
        // Route to appropriate handler based on command type
        if (commandType.find("MOUSE_") == 0) {
            result = executeMouseCommand(command, context);
        } else if (commandType.find("KEY_") == 0 || commandType.find("TYPE_") == 0) {
            result = executeKeyboardCommand(command, context);
        } else if (commandType.find("APP_") == 0) {
            result = executeApplicationCommand(command, context);
        } else if (commandType.find("SYSTEM_") == 0) {
            result = executeSystemCommand(command, context);
        } else if (commandType.find("WINDOW_") == 0) {
            result = executeWindowCommand(command, context);
        } else if (commandType.find("WAIT") == 0) {
            result = executeWaitCommand(command, context);
        } else if (commandType == "EXECUTE_SCRIPT") {
            result = executeScriptCommand(command, context);
        } else if (commandType.find("UIA_") == 0) {
            result = executeUiaCommand(command, context);
        } else if (commandType == "WHILE_LOOP") {
            result = executeWhileLoop(command, context);
        } else if (commandType == "SET_VARIABLE" || commandType == "GET_VARIABLE" || 
                   commandType == "IF_CONTAINS" || commandType == "IF_NOT_CONTAINS" ||
                   commandType == "IF_EQUALS" || commandType == "IF_NOT_EQUALS" ||
                   commandType == "CONDITIONAL_STOP" || commandType == "BREAK_IF") {
            result = executeControlCommand(command, context);
        } else {
            result.errorMessage = "Unknown command type: " + commandType;
        }
        
        // Store command result in context if it has a result variable
        if (result.success && command.contains("result_variable")) {
            std::string varName = command["result_variable"];
            context.variables[varName] = result.output;
        }
        
    } catch (const std::exception& e) {
        result.errorMessage = "Exception during command execution: " + std::string(e.what());
        SLOG_ERROR().message(result.errorMessage);
    }
    
    return result;
}

TaskExecutionResult ExecutionEngine::executeScriptFile(const std::string& scriptPath, ExecutionContext& context) {
    TaskExecutionResult result;
    result.success = false;
    
    // Validate script path
    auto validationResult = InputValidator::validateFilePath(scriptPath);
    if (!validationResult.isValid) {
        result.errorMessage = "Invalid script path: " + validationResult.errorMessage;
        return result;
    }
    
    // Load script file
    std::string scriptContent;
    if (!utils::FileUtils::readFileToString(scriptPath, scriptContent)) {
        result.errorMessage = "Failed to read script file: " + scriptPath;
        return result;
    }
    
    // Parse JSON
    nlohmann::json script;
    try {
        script = nlohmann::json::parse(scriptContent);
    } catch (const nlohmann::json::parse_error& e) {
        result.errorMessage = "Invalid JSON in script file: " + std::string(e.what());
        return result;
    }
    
    // Execute script commands - support both "commands" and "sequence" arrays
    if (script.contains("commands") && script["commands"].is_array()) {
        result = executeCommandSequence(script["commands"], context);
    } else if (script.contains("sequence") && script["sequence"].is_array()) {
        result = executeCommandSequence(script["sequence"], context);
    } else {
        result.errorMessage = "Script file must contain 'commands' or 'sequence' array";
    }
    
    return result;
}

std::string ExecutionEngine::substituteVariables(const std::string& input, const ExecutionContext& context) {
    std::string result = input;
    
    // Pattern for variable substitution: ${varName} or ${varName.field}
    std::regex varPattern(R"(\$\{([^}]+)\})");
    std::smatch match;
    std::string temp = result;
    
    while (std::regex_search(temp, match, varPattern)) {
        std::string varExpr = match[1].str();
        std::string replacement = evaluateVariableExpression(varExpr, context);
        
        // Replace the variable with its value
        size_t pos = result.find(match[0].str());
        if (pos != std::string::npos) {
            result.replace(pos, match[0].str().length(), replacement);
        }
        
        // Continue searching after the replacement
        temp = result.substr(pos + replacement.length());
    }
    
    return result;
}

nlohmann::json ExecutionEngine::substituteVariablesInParams(const nlohmann::json& params, const ExecutionContext& context) {
    if (params.is_string()) {
        return substituteVariables(params.get<std::string>(), context);
    } else if (params.is_object()) {
        nlohmann::json result = nlohmann::json::object();
        for (auto& [key, value] : params.items()) {
            result[key] = substituteVariablesInParams(value, context);
        }
        return result;
    } else if (params.is_array()) {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : params) {
            result.push_back(substituteVariablesInParams(item, context));
        }
        return result;
    } else {
        // Return as-is for numbers, booleans, null
        return params;
    }
}

bool ExecutionEngine::isCommandSafe(const nlohmann::json& command) {
    if (!command.contains("command")) {
        return false;
    }
    
    std::string commandType = command["command"];
    
    // List of potentially dangerous commands
    std::vector<std::string> dangerousCommands = {
        "SYSTEM_DELETE_FILE",
        "SYSTEM_FORMAT_DRIVE",
        "SYSTEM_MODIFY_REGISTRY",
        "SYSTEM_SHUTDOWN",
        "SYSTEM_RESTART"
    };
    
    for (const auto& dangerous : dangerousCommands) {
        if (commandType == dangerous) {
            return false;
        }
    }
    
    // Validate parameters for system commands
    if (commandType == "SYSTEM_RUN_COMMAND" && command.contains("parameters")) {
        auto params = command["parameters"];
        if (params.contains("command")) {
            std::string sysCmd = params["command"];
            // Check for dangerous shell commands
            if (InputValidator::containsShellMetacharacters(sysCmd)) {
                return false;
            }
        }
    }
    
    return true;
}

bool ExecutionEngine::requiresUserConfirmation(const nlohmann::json& command) {
    if (!isCommandSafe(command)) {
        return true;
    }
    
    std::string commandType = command["command"];
    
    // Commands that always require confirmation
    std::vector<std::string> confirmCommands = {
        "SYSTEM_RUN_COMMAND",
        "APP_CLOSE",
        "WINDOW_CLOSE_ALL",
        "EXECUTE_SCRIPT"
    };
    
    for (const auto& confirm : confirmCommands) {
        if (commandType == confirm) {
            return true;
        }
    }
    
    return false;
}

// Private helper methods

void ExecutionEngine::updateExecutionLog(ExecutionContext& context, const std::string& entry) {
    context.executionLog.push_back(entry);
}

std::string ExecutionEngine::formatCommandDescription(const nlohmann::json& command) {
    std::stringstream ss;
    ss << command["command"].get<std::string>();
    
    if (command.contains("description")) {
        ss << " - " << command["description"].get<std::string>();
    }
    
    if (command.contains("parameters")) {
        ss << " (";
        bool first = true;
        for (auto& [key, value] : command["parameters"].items()) {
            if (!first) ss << ", ";
            ss << key << "=" << value.dump();
            first = false;
        }
        ss << ")";
    }
    
    return ss.str();
}

bool ExecutionEngine::checkTimeout(double startTime, int timeoutMs) {
    double currentTime = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (currentTime - startTime) > timeoutMs;
}

std::string ExecutionEngine::evaluateVariableExpression(const std::string& expression, const ExecutionContext& context) {
    // Handle array access: varName[index]
    std::regex arrayPattern(R"((\w+)\[(\d+)\])");
    std::smatch arrayMatch;
    
    if (std::regex_match(expression, arrayMatch, arrayPattern)) {
        std::string varName = arrayMatch[1].str();
        int index = std::stoi(arrayMatch[2].str());
        
        auto it = context.variables.find(varName);
        if (it != context.variables.end() && it->second.is_array()) {
            if (index >= 0 && index < static_cast<int>(it->second.size())) {
                // Get the array element
                const auto& element = it->second[index];
                std::string value;
                
                // Return string values without quotes
                if (element.is_string()) {
                    value = element.get<std::string>();
                } else {
                    value = element.dump();
                }
                
                SLOG_DEBUG().message("Array access successful")
                    .context("expression", expression)
                    .context("value", value);
                return value;
            } else {
                SLOG_DEBUG().message("Array index out of bounds")
                    .context("expression", expression)
                    .context("array_size", it->second.size())
                    .context("requested_index", index);
            }
        } else {
            SLOG_DEBUG().message("Variable not found or not an array")
                .context("expression", expression)
                .context("var_exists", it != context.variables.end())
                .context("is_array", it != context.variables.end() ? it->second.is_array() : false);
        }
        return "";
    }
    
    // Handle nested field access: varName.field.subfield
    size_t dotPos = expression.find('.');
    if (dotPos != std::string::npos) {
        std::string varName = expression.substr(0, dotPos);
        std::string fieldPath = expression.substr(dotPos + 1);
        
        auto it = context.variables.find(varName);
        if (it != context.variables.end()) {
            // Navigate through nested fields
            nlohmann::json current = it->second;
            std::istringstream fieldStream(fieldPath);
            std::string field;
            
            while (std::getline(fieldStream, field, '.')) {
                if (current.is_object() && current.contains(field)) {
                    current = current[field];
                } else {
                    return "";
                }
            }
            
            if (current.is_string()) {
                return current.get<std::string>();
            } else {
                return current.dump();
            }
        }
    }
    
    // Simple variable lookup
    auto it = context.variables.find(expression);
    if (it != context.variables.end()) {
        if (it->second.is_string()) {
            return it->second.get<std::string>();
        } else {
            return it->second.dump();
        }
    }
    
    // Check sub-script results
    auto subIt = context.subScriptResults.find(expression);
    if (subIt != context.subScriptResults.end()) {
        if (subIt->second.is_string()) {
            return subIt->second.get<std::string>();
        } else {
            return subIt->second.dump();
        }
    }
    
    // Variable not found, return empty string
    return "";
}

bool ExecutionEngine::evaluateConditionExpression(const std::string& expression, const ExecutionContext& context) {
    // Simple condition evaluation
    // Support basic comparisons: var == value, var != value, var > value, etc.
    
    std::regex comparisonPattern(R"((.+?)\s*(==|!=|>|<|>=|<=)\s*(.+))");
    std::smatch match;
    
    if (std::regex_match(expression, match, comparisonPattern)) {
        std::string leftExpr = match[1].str();
        std::string op = match[2].str();
        std::string rightExpr = match[3].str();
        
        // Evaluate both sides
        std::string leftValue = substituteVariables("${" + leftExpr + "}", context);
        std::string rightValue = rightExpr;
        
        // If right side looks like a variable, evaluate it
        if (rightExpr.find("${") == 0) {
            rightValue = substituteVariables(rightExpr, context);
        }
        
        // Perform comparison
        if (op == "==") {
            return leftValue == rightValue;
        } else if (op == "!=") {
            return leftValue != rightValue;
        } else {
            // Try numeric comparison
            try {
                double leftNum = std::stod(leftValue);
                double rightNum = std::stod(rightValue);
                
                if (op == ">") return leftNum > rightNum;
                if (op == "<") return leftNum < rightNum;
                if (op == ">=") return leftNum >= rightNum;
                if (op == "<=") return leftNum <= rightNum;
            } catch (...) {
                // Fall back to string comparison
                if (op == ">") return leftValue > rightValue;
                if (op == "<") return leftValue < rightValue;
                if (op == ">=") return leftValue >= rightValue;
                if (op == "<=") return leftValue <= rightValue;
            }
        }
    }
    
    // Check for boolean variable
    std::string value = substituteVariables("${" + expression + "}", context);
    return value == "true" || value == "1";
}

// Placeholder implementations for command type handlers
// These would be implemented based on the actual command execution logic

TaskExecutionResult ExecutionEngine::executeMouseCommand(const nlohmann::json& command, ExecutionContext& context) {
    (void)command; // TODO: Implement mouse command execution
    (void)context; // TODO: Use context for mouse operations
    TaskExecutionResult result;
    result.success = true;
    // Implementation would use m_ocal for mouse operations
    return result;
}

TaskExecutionResult ExecutionEngine::executeKeyboardCommand(const nlohmann::json& command, ExecutionContext& context) {
    (void)command; // TODO: Implement keyboard command execution
    (void)context; // TODO: Use context for keyboard operations
    TaskExecutionResult result;
    result.success = true;
    // Implementation would use m_ocal for keyboard operations
    return result;
}

TaskExecutionResult ExecutionEngine::executeApplicationCommand(const nlohmann::json& command, ExecutionContext& context) {
    (void)command; // TODO: Implement application command execution
    (void)context; // TODO: Use context for application operations
    TaskExecutionResult result;
    result.success = true;
    // Implementation would use m_ocal for application operations
    return result;
}

TaskExecutionResult ExecutionEngine::executeSystemCommand(const nlohmann::json& command, ExecutionContext& context) {
    (void)command; // TODO: Implement system command execution
    (void)context; // TODO: Use context for system operations
    TaskExecutionResult result;
    result.success = true;
    // Implementation would use m_ocal for system operations
    return result;
}

TaskExecutionResult ExecutionEngine::executeWindowCommand(const nlohmann::json& command, ExecutionContext& context) {
    (void)command; // TODO: Implement window command execution
    (void)context; // TODO: Use context for window operations
    TaskExecutionResult result;
    result.success = true;
    // Implementation would use m_ocal for window operations
    return result;
}

TaskExecutionResult ExecutionEngine::executeWaitCommand(const nlohmann::json& command, ExecutionContext& context) {
    (void)context; // TODO: Use context for wait operations
    TaskExecutionResult result;
    result.success = true;
    
    if (command.contains("parameters")) {
        auto params = command["parameters"];
        int duration_ms = 0;
        
        // Support both "duration_ms" and "duration"
        if (params.contains("duration_ms")) {
            duration_ms = params["duration_ms"];
        } else if (params.contains("duration")) {
            // Parse duration string like "200ms"
            auto duration_str = params["duration"];
            if (duration_str.is_string()) {
                std::string dur = duration_str.get<std::string>();
                // Simple parsing - just extract the number
                duration_ms = std::stoi(dur);
            } else if (duration_str.is_number()) {
                duration_ms = duration_str.get<int>();
            }
        }
        
        if (duration_ms > 0) {
            // Make wait interruptible by checking shutdown flag periodically
            auto end_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
            
            while (std::chrono::steady_clock::now() < end_time) {
                if (ShutdownManager::getInstance().isShutdownRequested()) {
                    result.success = false;
                    result.errorMessage = "Wait interrupted by shutdown signal";
                    return result;
                }
                
                // Sleep in small increments to check shutdown flag
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }
    
    return result;
}

TaskExecutionResult ExecutionEngine::executeControlCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    
    try {
        std::string commandType = command["command"];
        auto params = command.contains("parameters") ? command["parameters"] : nlohmann::json::object();
        
        if (commandType == "SET_VARIABLE") {
            // Set a variable in the context
            if (!params.contains("name") || !params.contains("value")) {
                result.errorMessage = "SET_VARIABLE requires 'name' and 'value' parameters";
                result.success = false;
                return result;
            }
            
            std::string varName = params["name"];
            context.variables[varName] = params["value"];
            result.success = true;
            result.output = "Variable '" + varName + "' set";
            
        } else if (commandType == "GET_VARIABLE") {
            // Get a variable from the context
            if (!params.contains("name")) {
                result.errorMessage = "GET_VARIABLE requires 'name' parameter";
                result.success = false;
                return result;
            }
            
            std::string varName = params["name"];
            if (context.variables.find(varName) != context.variables.end()) {
                result.output = context.variables[varName].dump();
                result.success = true;
            } else {
                result.errorMessage = "Variable '" + varName + "' not found";
                result.success = false;
            }
            
        } else if (commandType == "IF_NOT_CONTAINS") {
            // Check if a variable does NOT contain a substring (case-insensitive)
            if (!params.contains("variable") || !params.contains("substring")) {
                result.errorMessage = "IF_NOT_CONTAINS requires 'variable' and 'substring' parameters";
                result.success = false;
                return result;
            }
            
            std::string varName = params["variable"];
            std::string substring = params["substring"];
            
            // Get variable value from context
            std::string variableValue = "";
            if (context.variables.find(varName) != context.variables.end()) {
                if (context.variables[varName].is_string()) {
                    variableValue = context.variables[varName].get<std::string>();
                } else {
                    variableValue = context.variables[varName].dump();
                }
                SLOG_DEBUG().message("Variable found in context")
                    .context("variable", varName)
                    .context("type", context.variables[varName].type_name())
                    .context("raw_dump", context.variables[varName].dump());
            } else {
                SLOG_DEBUG().message("Variable NOT found in context")
                    .context("variable", varName);
            }
            
            // Convert both to lowercase for case-insensitive comparison
            std::string lowerValue = variableValue;
            std::string lowerSubstring = substring;
            std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
            std::transform(lowerSubstring.begin(), lowerSubstring.end(), lowerSubstring.begin(), ::tolower);
            
            bool notContains = (lowerValue.find(lowerSubstring) == std::string::npos);
            
            SLOG_DEBUG().message("IF_NOT_CONTAINS check")
                .context("variable", varName)
                .context("value", variableValue)
                .context("substring", substring)
                .context("result", notContains);
            
            // Store result in context if requested
            if (params.contains("store_as")) {
                std::string resultVar = params["store_as"];
                context.variables[resultVar] = notContains;
            }
            
            result.success = true;
            result.output = notContains ? "true" : "false";
            
        } else if (commandType == "IF_CONTAINS") {
            // Check if a variable contains a substring (case-insensitive)
            if (!params.contains("variable") || !params.contains("substring")) {
                result.errorMessage = "IF_CONTAINS requires 'variable' and 'substring' parameters";
                result.success = false;
                return result;
            }
            
            std::string varName = params["variable"];
            std::string substring = params["substring"];
            
            // Get variable value from context
            std::string variableValue = "";
            if (context.variables.find(varName) != context.variables.end()) {
                if (context.variables[varName].is_string()) {
                    variableValue = context.variables[varName].get<std::string>();
                } else {
                    variableValue = context.variables[varName].dump();
                }
                SLOG_DEBUG().message("Variable found in context")
                    .context("variable", varName)
                    .context("type", context.variables[varName].type_name())
                    .context("raw_dump", context.variables[varName].dump());
            } else {
                SLOG_DEBUG().message("Variable NOT found in context")
                    .context("variable", varName);
            }
            
            // Convert both to lowercase for case-insensitive comparison
            std::string lowerValue = variableValue;
            std::string lowerSubstring = substring;
            std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
            std::transform(lowerSubstring.begin(), lowerSubstring.end(), lowerSubstring.begin(), ::tolower);
            
            bool contains = (lowerValue.find(lowerSubstring) != std::string::npos);
            
            SLOG_DEBUG().message("IF_CONTAINS check")
                .context("variable", varName)
                .context("value", variableValue)
                .context("substring", substring)
                .context("result", contains);
            
            // Store result in context if requested
            if (params.contains("store_as")) {
                std::string resultVar = params["store_as"];
                context.variables[resultVar] = contains;
            }
            
            result.success = true;
            result.output = contains ? "true" : "false";
            
        } else if (commandType == "IF_EQUALS") {
            // Check if a variable equals a value (case-insensitive)
            if (!params.contains("variable") || !params.contains("value")) {
                result.errorMessage = "IF_EQUALS requires 'variable' and 'value' parameters";
                result.success = false;
                return result;
            }
            
            std::string varName = params["variable"];
            std::string compareValue = params["value"];
            
            // Get variable value from context
            std::string variableValue = "";
            if (context.variables.find(varName) != context.variables.end()) {
                if (context.variables[varName].is_string()) {
                    variableValue = context.variables[varName].get<std::string>();
                } else {
                    variableValue = context.variables[varName].dump();
                }
                SLOG_DEBUG().message("Variable found in context")
                    .context("variable", varName)
                    .context("type", context.variables[varName].type_name())
                    .context("raw_dump", context.variables[varName].dump());
            } else {
                SLOG_DEBUG().message("Variable NOT found in context")
                    .context("variable", varName);
            }
            
            // Convert both to lowercase for case-insensitive comparison
            std::string lowerValue = variableValue;
            std::string lowerCompare = compareValue;
            std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
            std::transform(lowerCompare.begin(), lowerCompare.end(), lowerCompare.begin(), ::tolower);
            
            bool equals = (lowerValue == lowerCompare);
            
            SLOG_DEBUG().message("IF_EQUALS check")
                .context("variable", varName)
                .context("value", variableValue)
                .context("compare_to", compareValue)
                .context("result", equals);
            
            // Store result in context if requested
            if (params.contains("store_as")) {
                std::string resultVar = params["store_as"];
                context.variables[resultVar] = equals;
            }
            
            result.success = true;
            result.output = equals ? "true" : "false";
            
        } else if (commandType == "IF_NOT_EQUALS") {
            // Check if a variable does not equal a value (case-insensitive)
            if (!params.contains("variable") || !params.contains("value")) {
                result.errorMessage = "IF_NOT_EQUALS requires 'variable' and 'value' parameters";
                result.success = false;
                return result;
            }
            
            std::string varName = params["variable"];
            std::string compareValue = params["value"];
            
            // Get variable value from context
            std::string variableValue = "";
            if (context.variables.find(varName) != context.variables.end()) {
                if (context.variables[varName].is_string()) {
                    variableValue = context.variables[varName].get<std::string>();
                } else {
                    variableValue = context.variables[varName].dump();
                }
                SLOG_DEBUG().message("Variable found in context")
                    .context("variable", varName)
                    .context("type", context.variables[varName].type_name())
                    .context("raw_dump", context.variables[varName].dump());
            } else {
                SLOG_DEBUG().message("Variable NOT found in context")
                    .context("variable", varName);
            }
            
            // Convert both to lowercase for case-insensitive comparison
            std::string lowerValue = variableValue;
            std::string lowerCompare = compareValue;
            std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
            std::transform(lowerCompare.begin(), lowerCompare.end(), lowerCompare.begin(), ::tolower);
            
            bool notEquals = (lowerValue != lowerCompare);
            
            SLOG_DEBUG().message("IF_NOT_EQUALS check")
                .context("variable", varName)
                .context("value", variableValue)
                .context("compare_to", compareValue)
                .context("result", notEquals);
            
            // Store result in context if requested
            if (params.contains("store_as")) {
                std::string resultVar = params["store_as"];
                context.variables[resultVar] = notEquals;
            }
            
            result.success = true;
            result.output = notEquals ? "true" : "false";
            
        } else if (commandType == "CONDITIONAL_STOP") {
            // Stop execution if condition is true (with optional invert)
            if (!params.contains("condition_variable")) {
                result.errorMessage = "CONDITIONAL_STOP requires 'condition_variable' parameter";
                result.success = false;
                return result;
            }
            
            std::string conditionVar = params["condition_variable"];
            bool invertCondition = params.value("invert", false);
            
            // Get condition value from context
            bool conditionValue = false;
            if (context.variables.find(conditionVar) != context.variables.end()) {
                auto& value = context.variables[conditionVar];
                if (value.is_boolean()) {
                    conditionValue = value.get<bool>();
                } else if (value.is_string()) {
                    std::string strValue = value.get<std::string>();
                    std::transform(strValue.begin(), strValue.end(), strValue.begin(), ::tolower);
                    conditionValue = (strValue == "true" || strValue == "1" || strValue == "yes");
                }
            }
            
            // Apply invert logic if specified
            if (invertCondition) {
                conditionValue = !conditionValue;
            }
            
            SLOG_DEBUG().message("CONDITIONAL_STOP check")
                .context("condition_variable", conditionVar)
                .context("condition_value", conditionValue)
                .context("inverted", invertCondition)
                .context("will_stop", conditionValue);
            
            if (conditionValue) {
                // Set a special flag to indicate script should stop
                result.success = false;
                result.errorMessage = "Script execution stopped by CONDITIONAL_STOP";
                result.output = "stopped";
            } else {
                result.success = true;
                result.output = "continue";
            }
            
        } else if (commandType == "BREAK_IF") {
            // Break out of loop if condition is true
            if (!params.contains("condition_variable")) {
                result.errorMessage = "BREAK_IF requires 'condition_variable' parameter";
                result.success = false;
                return result;
            }
            
            std::string conditionVar = params["condition_variable"];
            
            // Get condition value from context
            bool conditionValue = false;
            if (context.variables.find(conditionVar) != context.variables.end()) {
                auto& value = context.variables[conditionVar];
                if (value.is_boolean()) {
                    conditionValue = value.get<bool>();
                } else if (value.is_string()) {
                    std::string strValue = value.get<std::string>();
                    std::transform(strValue.begin(), strValue.end(), strValue.begin(), ::tolower);
                    conditionValue = (strValue == "true" || strValue == "1" || strValue == "yes");
                }
            }
            
            SLOG_DEBUG().message("BREAK_IF check")
                .context("condition_variable", conditionVar)
                .context("condition_value", conditionValue);
            
            if (conditionValue) {
                // Signal loop break
                result.success = false;
                result.status = ExecutionStatus::BREAK_LOOP;
                result.output = "break";
            } else {
                result.success = true;
                result.output = "continue";
            }
            
        } else {
            result.errorMessage = "Unknown control command: " + commandType;
            result.success = false;
        }
        
    } catch (const std::exception& e) {
        result.errorMessage = "Error in control command: " + std::string(e.what());
        result.success = false;
    }
    
    return result;
}

TaskExecutionResult ExecutionEngine::executeScriptCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    
    if (!command.contains("parameters") || !command["parameters"].contains("script_path")) {
        result.success = false;
        result.errorMessage = "Script command missing script_path parameter";
        return result;
    }
    
    std::string scriptPath = command["parameters"]["script_path"];
    
    // Create a child context with inherited variables
    ExecutionContext childContext = context;
    
    // Check if variables are passed to the nested script
    if (command["parameters"].contains("variables") && command["parameters"]["variables"].is_object()) {
        // Add the passed variables to the child context
        for (auto& [key, value] : command["parameters"]["variables"].items()) {
            // Substitute variables in the value before setting
            if (value.is_string()) {
                std::string substitutedValue = substituteVariables(value.get<std::string>(), context);
                childContext.variables[key] = substitutedValue;
            } else {
                childContext.variables[key] = value;
            }
        }
    }
    
    // Execute the script with the child context
    result = executeScriptFile(scriptPath, childContext);
    
    // Store result in parent context if result_variable is specified
    if (result.success && command["parameters"].contains("result_variable")) {
        std::string resultVar = command["parameters"]["result_variable"];
        // Try to get the result from child context variables first
        if (childContext.variables.find("result") != childContext.variables.end()) {
            context.variables[resultVar] = childContext.variables["result"];
        } else if (!result.output.empty()) {
            context.variables[resultVar] = result.output;
        }
        
        // Also copy back any variables that were set in the child script
        // This is important for scripts that set variables we need to access later
        for (auto& [key, value] : childContext.variables) {
            // Don't overwrite parent variables unless they were explicitly set in child
            if (context.variables.find(key) == context.variables.end() || 
                context.variables[key] != value) {
                context.variables[key] = value;
            }
        }
    }
    
    return result;
}

TaskExecutionResult ExecutionEngine::executeUiaCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    result.success = false;
    
    if (!m_ocal) {
        result.errorMessage = "OCAL not initialized";
        return result;
    }
    
    std::string commandType = command["command"];
    nlohmann::json params = command.contains("parameters") ? command["parameters"] : nlohmann::json::object();
    
    try {
        if (commandType == "UIA_ENUM_WINDOWS") {
            // Enumerate all windows
            std::map<uintptr_t, std::map<std::string, std::string>> windowResults;
            
            if (::ocal::atomic::window::enumerateWindows(windowResults)) {
                SLOG_DEBUG().message("Enumerated all windows")
                    .context("total_count", windowResults.size());
                
                // TODO: Remove debug logging after Firefox detection issue is resolved
                // Log a few window details for debugging
                int debugCount = 0;
                for (const auto& [hwnd, info] : windowResults) {
                    if (debugCount < 10) {
                        SLOG_DEBUG().message("Window found")
                            .context("hwnd", hwnd)
                            .context("title", info.at("title"))
                            .context("className", info.at("className"))
                            .context("isVisible", info.at("isVisible"))
                            .context("isMinimized", info.at("isMinimized"));
                        debugCount++;
                    }
                    // TODO: Remove special Mozilla check after debugging
                    // Special check for Mozilla windows
                    if (info.at("className") == "MozillaWindowClass") {
                        SLOG_INFO().message("Found Mozilla window!")
                            .context("hwnd", hwnd)
                            .context("title", info.at("title"))
                            .context("isVisible", info.at("isVisible"))
                            .context("isMinimized", info.at("isMinimized"))
                            .context("isToolWindow", info.at("isToolWindow"));
                    }
                }
                
                // Apply class name filtering if specified
                std::string classNameFilter = params.value("class_name", "");
                std::map<uintptr_t, std::map<std::string, std::string>> filteredResults;
                
                if (!classNameFilter.empty()) {
                    SLOG_DEBUG().message("Filtering windows by class name")
                        .context("class_name", classNameFilter);
                    
                    bool excludeToolWindows = params.value("exclude_tool_windows", true);
                    
                    for (const auto& [hwnd, info] : windowResults) {
                        if (info.at("className") == classNameFilter) {
                            // Check if we should exclude tool windows
                            if (excludeToolWindows && info.at("isToolWindow") == "true") {
                                SLOG_DEBUG().message("Excluding tool window")
                                    .context("hwnd", hwnd)
                                    .context("title", info.at("title"));
                                continue;
                            }
                            
                            filteredResults[hwnd] = info;
                            SLOG_DEBUG().message("Found matching window")
                                .context("hwnd", hwnd)
                                .context("title", info.at("title"))
                                .context("isToolWindow", info.at("isToolWindow"));
                        }
                    }
                    windowResults = filteredResults;
                }
                
                result.success = true;
                result.output = "Found " + std::to_string(windowResults.size()) + " windows";
                
                // Store window handles in variable if specified
                std::string varName = params.value("store_as", "");
                if (!varName.empty()) {
                    nlohmann::json windowArray = nlohmann::json::array();
                    for (const auto& [hwnd, info] : windowResults) {
                        std::string windowHandle = std::to_string(hwnd);
                        windowArray.push_back(windowHandle);
                    }
                    context.variables[varName] = windowArray;
                    SLOG_DEBUG().message("Stored window handles in variable")
                        .context("variable", varName)
                        .context("count", windowResults.size())
                        .context("value", windowArray.dump());
                }
            } else {
                result.errorMessage = "Failed to enumerate windows";
            }
            
        } else if (commandType == "UIA_FOCUS_WINDOW") {
            // Focus a window
            std::string hwndStr = substituteVariables(params.value("hwnd", ""), context);
            if (hwndStr.empty()) {
                result.errorMessage = "UIA_FOCUS_WINDOW requires 'hwnd' parameter";
                return result;
            }
            
            uintptr_t hwnd = std::stoull(hwndStr);
            if (::ocal::atomic::window::focusWindow(hwnd)) {
                result.success = true;
                result.output = "Window focused";
            } else {
                result.errorMessage = "Failed to focus window";
            }
            
        } else if (commandType == "UIA_GET_WINDOW_TITLE") {
            // Get window title
            std::string hwndStr = substituteVariables(params.value("hwnd", ""), context);
            if (hwndStr.empty()) {
                result.errorMessage = "UIA_GET_WINDOW_TITLE requires 'hwnd' parameter";
                return result;
            }
            
            uintptr_t hwnd = std::stoull(hwndStr);
            std::string title;
            if (::ocal::atomic::window::getWindowTitle(hwnd, title)) {
                result.success = true;
                result.output = title;
                
                // Store in variable if specified
                std::string varName = params.value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = title;
                }
            } else {
                result.errorMessage = "Failed to get window title";
            }
            
        } else if (commandType == "UIA_SET_CLIPBOARD") {
            // Set clipboard content
            std::string text = substituteVariables(params.value("text", ""), context);
            if (::ocal::atomic::input::setClipboard(text)) {
                result.success = true;
                result.output = "Clipboard set";
            } else {
                result.errorMessage = "Failed to set clipboard";
            }
            
        } else if (commandType == "UIA_GET_CLIPBOARD") {
            // Get clipboard content
            std::string text;
            if (::ocal::atomic::input::getClipboard(text)) {
                result.success = true;
                result.output = text;
                
                // Store in variable if specified
                std::string varName = params.value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = text;
                }
            } else {
                result.errorMessage = "Failed to get clipboard";
            }
            
        } else if (commandType == "UIA_KEY_PRESS") {
            // Press a key
            std::string key = substituteVariables(params.value("key", ""), context);
            if (key.empty()) {
                result.errorMessage = "UIA_KEY_PRESS requires 'key' parameter";
                return result;
            }
            
            // Convert key string to virtual key code
            unsigned char vkCode = 0;
            if (key == "VK_CONTROL") vkCode = VK_CONTROL;
            else if (key == "VK_SHIFT") vkCode = VK_SHIFT;
            else if (key == "VK_MENU" || key == "VK_ALT") vkCode = VK_MENU;
            else if (key == "VK_LWIN") vkCode = VK_LWIN;
            else if (key == "VK_TAB") vkCode = VK_TAB;
            else if (key == "VK_RETURN") vkCode = VK_RETURN;
            else if (key == "VK_ESCAPE") vkCode = VK_ESCAPE;
            else if (key == "VK_SPACE") vkCode = VK_SPACE;
            else if (key == "VK_BACK") vkCode = VK_BACK;
            else if (key == "VK_DELETE") vkCode = VK_DELETE;
            else if (key == "VK_F1") vkCode = VK_F1;
            else if (key == "VK_F2") vkCode = VK_F2;
            else if (key == "VK_F3") vkCode = VK_F3;
            else if (key == "VK_F4") vkCode = VK_F4;
            else if (key == "VK_F5") vkCode = VK_F5;
            else if (key == "VK_F6") vkCode = VK_F6;
            else if (key == "VK_F7") vkCode = VK_F7;
            else if (key == "VK_F8") vkCode = VK_F8;
            else if (key == "VK_F9") vkCode = VK_F9;
            else if (key == "VK_F10") vkCode = VK_F10;
            else if (key == "VK_F11") vkCode = VK_F11;
            else if (key == "VK_F12") vkCode = VK_F12;
            else if (key == "VK_LEFT") vkCode = VK_LEFT;
            else if (key == "VK_RIGHT") vkCode = VK_RIGHT;
            else if (key == "VK_UP") vkCode = VK_UP;
            else if (key == "VK_DOWN") vkCode = VK_DOWN;
            else if (key == "VK_HOME") vkCode = VK_HOME;
            else if (key == "VK_END") vkCode = VK_END;
            else if (key == "VK_PRIOR" || key == "VK_PAGEUP") vkCode = VK_PRIOR;
            else if (key == "VK_NEXT" || key == "VK_PAGEDOWN") vkCode = VK_NEXT;
            else if (key.length() == 1) {
                // Single character key
                vkCode = VkKeyScan(key[0]) & 0xFF;
            } else {
                result.errorMessage = "Invalid key: " + key;
                return result;
            }
            
            if (::ocal::atomic::input::pressKey(vkCode)) {
                result.success = true;
                result.output = "Key pressed";
            } else {
                result.errorMessage = "Failed to press key";
            }
            
        } else if (commandType == "UIA_KEY_RELEASE") {
            // Release a key
            std::string key = substituteVariables(params.value("key", ""), context);
            if (key.empty()) {
                result.errorMessage = "UIA_KEY_RELEASE requires 'key' parameter";
                return result;
            }
            
            // Convert key string to virtual key code (same as above)
            unsigned char vkCode = 0;
            if (key == "VK_CONTROL") vkCode = VK_CONTROL;
            else if (key == "VK_SHIFT") vkCode = VK_SHIFT;
            else if (key == "VK_MENU" || key == "VK_ALT") vkCode = VK_MENU;
            else if (key == "VK_LWIN") vkCode = VK_LWIN;
            else if (key == "VK_TAB") vkCode = VK_TAB;
            else if (key == "VK_RETURN") vkCode = VK_RETURN;
            else if (key == "VK_ESCAPE") vkCode = VK_ESCAPE;
            else if (key == "VK_SPACE") vkCode = VK_SPACE;
            else if (key == "VK_BACK") vkCode = VK_BACK;
            else if (key == "VK_DELETE") vkCode = VK_DELETE;
            else if (key == "VK_F1") vkCode = VK_F1;
            else if (key == "VK_F2") vkCode = VK_F2;
            else if (key == "VK_F3") vkCode = VK_F3;
            else if (key == "VK_F4") vkCode = VK_F4;
            else if (key == "VK_F5") vkCode = VK_F5;
            else if (key == "VK_F6") vkCode = VK_F6;
            else if (key == "VK_F7") vkCode = VK_F7;
            else if (key == "VK_F8") vkCode = VK_F8;
            else if (key == "VK_F9") vkCode = VK_F9;
            else if (key == "VK_F10") vkCode = VK_F10;
            else if (key == "VK_F11") vkCode = VK_F11;
            else if (key == "VK_F12") vkCode = VK_F12;
            else if (key == "VK_LEFT") vkCode = VK_LEFT;
            else if (key == "VK_RIGHT") vkCode = VK_RIGHT;
            else if (key == "VK_UP") vkCode = VK_UP;
            else if (key == "VK_DOWN") vkCode = VK_DOWN;
            else if (key == "VK_HOME") vkCode = VK_HOME;
            else if (key == "VK_END") vkCode = VK_END;
            else if (key == "VK_PRIOR" || key == "VK_PAGEUP") vkCode = VK_PRIOR;
            else if (key == "VK_NEXT" || key == "VK_PAGEDOWN") vkCode = VK_NEXT;
            else if (key.length() == 1) {
                // Single character key
                vkCode = VkKeyScan(key[0]) & 0xFF;
            } else {
                result.errorMessage = "Invalid key: " + key;
                return result;
            }
            
            if (::ocal::atomic::input::releaseKey(vkCode)) {
                result.success = true;
                result.output = "Key released";
            } else {
                result.errorMessage = "Failed to release key";
            }
            
        } else if (commandType == "UIA_MOUSE_CLICK") {
            // Mouse click
            int x = params.value("x", 0);
            int y = params.value("y", 0);
            std::string button = params.value("button", "left");
            
            // First move to position if coordinates provided
            if (x != 0 || y != 0) {
                ::ocal::atomic::input::mouseMove(x, y);
            }
            
            if (::ocal::atomic::input::mouseClick(button)) {
                result.success = true;
                result.output = "Mouse clicked";
            } else {
                result.errorMessage = "Failed to click mouse";
            }
            
        } else if (commandType == "UIA_MOUSE_MOVE") {
            // Mouse move
            int x = params.value("x", 0);
            int y = params.value("y", 0);
            
            if (::ocal::atomic::input::mouseMove(x, y)) {
                result.success = true;
                result.output = "Mouse moved";
            } else {
                result.errorMessage = "Failed to move mouse";
            }
            
        } else if (commandType == "UIA_WINDOW_RESIZE") {
            // Resize window
            std::string hwndStr = substituteVariables(params.value("hwnd", ""), context);
            if (hwndStr.empty()) {
                result.errorMessage = "UIA_WINDOW_RESIZE requires 'hwnd' parameter";
                return result;
            }
            
            uintptr_t hwnd = std::stoull(hwndStr);
            int width = params.value("width", 0);
            int height = params.value("height", 0);
            
            if (width <= 0 || height <= 0) {
                result.errorMessage = "UIA_WINDOW_RESIZE requires positive 'width' and 'height' parameters";
                return result;
            }
            
            if (::ocal::atomic::window::resizeWindow(hwnd, width, height)) {
                result.success = true;
                result.output = "Window resized";
            } else {
                result.errorMessage = "Failed to resize window";
            }
            
        } else if (commandType == "UIA_WINDOW_MOVE") {
            // Move window
            std::string hwndStr = substituteVariables(params.value("hwnd", ""), context);
            if (hwndStr.empty()) {
                result.errorMessage = "UIA_WINDOW_MOVE requires 'hwnd' parameter";
                return result;
            }
            
            uintptr_t hwnd = std::stoull(hwndStr);
            int x = params.value("x", 0);
            int y = params.value("y", 0);
            
            if (::ocal::atomic::window::moveWindow(hwnd, x, y)) {
                result.success = true;
                result.output = "Window moved";
            } else {
                result.errorMessage = "Failed to move window";
            }
            
        } else if (commandType == "UIA_GET_WINDOW_CLASS") {
            // Get window class
            std::string hwndStr = substituteVariables(params.value("hwnd", ""), context);
            if (hwndStr.empty()) {
                result.errorMessage = "UIA_GET_WINDOW_CLASS requires 'hwnd' parameter";
                return result;
            }
            
            uintptr_t hwnd = std::stoull(hwndStr);
            std::string className;
            if (::ocal::atomic::window::getWindowClass(hwnd, className)) {
                result.success = true;
                result.output = className;
                
                // Store in variable if specified
                std::string varName = params.value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = className;
                }
            } else {
                result.errorMessage = "Failed to get window class";
            }
            
        } else if (commandType == "UIA_GET_WINDOW_RECT") {
            // Get window rectangle
            std::string hwndStr = substituteVariables(params.value("hwnd", ""), context);
            if (hwndStr.empty()) {
                result.errorMessage = "UIA_GET_WINDOW_RECT requires 'hwnd' parameter";
                return result;
            }
            
            uintptr_t hwnd = std::stoull(hwndStr);
            std::map<std::string, int> rect;
            if (::ocal::atomic::window::getWindowRect(hwnd, rect)) {
                nlohmann::json rectJson;
                for (const auto& [key, value] : rect) {
                    rectJson[key] = value;
                }
                result.success = true;
                result.output = rectJson.dump();
                
                // Store in variable if specified
                std::string varName = params.value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = rectJson;
                }
            } else {
                result.errorMessage = "Failed to get window rect";
            }
            
        } else {
            result.errorMessage = "Unknown UIA command: " + commandType;
        }
        
    } catch (const std::exception& e) {
        result.errorMessage = "Exception in UIA command: " + std::string(e.what());
        SLOG_ERROR().message(result.errorMessage);
    }
    
    return result;
}

TaskExecutionResult ExecutionEngine::executeWhileLoop(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    result.success = false;
    
    if (!command.contains("parameters")) {
        result.errorMessage = "While loop missing parameters";
        return result;
    }
    
    auto params = command["parameters"];
    
    // Support both "commands" and "sequence" for the loop body
    nlohmann::json commands;
    if (params.contains("commands")) {
        commands = params["commands"];
    } else if (params.contains("sequence")) {
        commands = params["sequence"];
    } else {
        result.errorMessage = "While loop missing commands or sequence";
        return result;
    }
    
    // Support both "condition" and "always_true" 
    bool hasCondition = params.contains("condition");
    bool alwaysTrue = params.value("always_true", false);
    
    if (!hasCondition && !alwaysTrue) {
        result.errorMessage = "While loop missing condition or always_true";
        return result;
    }
    
    int maxIterations = 1000; // Safety limit
    if (params.contains("max_iterations")) {
        maxIterations = params["max_iterations"];
    }
    
    int iteration = 0;
    
    while (iteration < maxIterations) {
        // Check for shutdown request
        if (ShutdownManager::getInstance().isShutdownRequested()) {
            result.errorMessage = "Loop execution cancelled by user";
            result.status = ExecutionStatus::CANCELLED;
            return result;
        }
        
        // Evaluate loop condition
        bool shouldContinue = false;
        if (alwaysTrue) {
            shouldContinue = true;
        } else if (hasCondition) {
            shouldContinue = evaluateLoopCondition(params["condition"], context);
        }
        
        if (!shouldContinue) {
            break;
        }
        
        auto loopResult = executeCommandSequence(commands, context);
        
        if (!loopResult.success) {
            // Check for break or continue
            if (loopResult.status == ExecutionStatus::BREAK_LOOP) {
                result.success = true;
                break;
            } else if (loopResult.status == ExecutionStatus::CONTINUE_LOOP) {
                iteration++;
                continue;
            } else {
                // Actual error
                return loopResult;
            }
        }
        
        iteration++;
    }
    
    if (iteration >= maxIterations) {
        result.errorMessage = "While loop exceeded maximum iterations";
    } else {
        result.success = true;
    }
    
    return result;
}

bool ExecutionEngine::evaluateLoopCondition(const nlohmann::json& condition, ExecutionContext& context) {
    if (condition.is_string()) {
        return evaluateConditionExpression(condition.get<std::string>(), context);
    } else if (condition.is_boolean()) {
        return condition.get<bool>();
    }
    
    return false;
}

} // namespace burwell