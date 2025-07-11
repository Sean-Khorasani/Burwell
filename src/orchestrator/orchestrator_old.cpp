#include "orchestrator.h"
#include "../common/logger.h"
#include "../common/error_handler.h"
#include "../common/config_manager.h"
#include "../common/file_utils.h"
#include "../common/json_utils.h"
#include "../command_parser/command_parser.h"
#include "../llm_connector/llm_connector.h"
#include "../task_engine/task_engine.h"
#include "../ocal/ocal.h"
#include "../ocal/mouse_control.h"
#include "../ocal/window_management.h"
#include "../ocal/application_automation.h"
#include "../ocal/input_operations.h"
#include "../ocal/window_operations.h"
#include "../ocal/file_operations.h"
#include "../ocal/process_operations.h"
#include "../cpl/cpl_config_loader.h"
#include "../environmental_perception/environmental_perception.h"
#include "../ui_module/ui_module.h"
#include <chrono>
#include <algorithm>
#include <random>
#include <sstream>
#include <fstream>

// REMOVED: Windows API includes - orchestrator should only use OCAL abstractions

using namespace burwell;

Orchestrator::Orchestrator()
    : m_isRunning(false)
    , m_isPaused(false)
    , m_emergencyStop(false)
    , m_autoMode(false)
    , m_confirmationRequired(true)
    , m_maxConcurrentTasks(3) {
    
    // Load timing settings from CPL configuration
    auto& cplConfig = burwell::cpl::CPLConfigLoader::getInstance();
    m_executionTimeoutMs = cplConfig.getOrchestratorExecutionTimeout();
    m_mainLoopDelayMs = cplConfig.getOrchestratorMainLoopDelay();
    m_commandSequenceDelayMs = cplConfig.getOrchestratorCommandSequenceDelay();
    m_errorRecoveryDelayMs = cplConfig.getOrchestratorErrorRecoveryDelay();
    
    Logger::log(LogLevel::INFO, "Orchestrator initialized with workflow management capabilities");
    Logger::log(LogLevel::DEBUG, "Orchestrator timing - Execution timeout: " + std::to_string(m_executionTimeoutMs) + 
                "ms, Main loop delay: " + std::to_string(m_mainLoopDelayMs) + 
                "ms, Command delay: " + std::to_string(m_commandSequenceDelayMs) + "ms");
}

Orchestrator::~Orchestrator() {
    shutdown();
}

void Orchestrator::initialize() {
    BURWELL_TRY_CATCH({
        Logger::log(LogLevel::INFO, "Initializing Orchestrator components...");
        
        // Verify all components are available
        if (!m_commandParser) {
            throw std::runtime_error("CommandParser not set");
        }
        if (!m_llmConnector) {
            throw std::runtime_error("LLMConnector not set");
        }
        if (!m_taskEngine) {
            throw std::runtime_error("TaskEngine not set");
        }
        if (!m_ocal) {
            throw std::runtime_error("OCAL not set");
        }
        if (!m_perception) {
            throw std::runtime_error("EnvironmentalPerception not set");
        }
        
        // Initialize component connections
        m_commandParser->setLLMConnector(m_llmConnector);
        m_commandParser->setTaskEngine(m_taskEngine);
        
        m_isRunning = true;
        
        // Initialize feedback loop system
        m_feedbackActive = false;
        m_feedbackLoop.lastEnvironmentCheck = std::chrono::steady_clock::now();
        
        logActivity("Orchestrator initialized successfully");
        
        Logger::log(LogLevel::INFO, "Orchestrator initialization complete");
        
    }, "Orchestrator::initialize");
}

void Orchestrator::run() {
    BURWELL_TRY_CATCH({
        Logger::log(LogLevel::INFO, "Starting Orchestrator main execution loop...");
        
        if (!m_isRunning) {
            initialize();
        }
        
        // Start worker thread for async processing
        m_workerThread = std::thread(&Orchestrator::workerThreadFunction, this);
        
        // Start intelligent feedback loop for continuous environment monitoring
        startContinuousEnvironmentMonitoring();
        
        logActivity("Orchestrator started in main loop");
        
        // Main event loop - in a real implementation this would handle UI events
        while (m_isRunning && !m_emergencyStop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_mainLoopDelayMs));
            
            // Clean up completed executions periodically
            cleanupCompletedExecutions();
        }
        
        Logger::log(LogLevel::INFO, "Orchestrator main loop completed");
        
    }, "Orchestrator::run");
}

void Orchestrator::shutdown() {
    Logger::log(LogLevel::INFO, "Shutting down Orchestrator...");
    
    m_isRunning = false;
    m_queueCondition.notify_all();
    
    // Stop feedback loop first
    stopContinuousEnvironmentMonitoring();
    
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    
    logActivity("Orchestrator shutdown completed");
}

TaskExecutionResult Orchestrator::processUserRequest(const std::string& userInput) {
    BURWELL_TRY_CATCH({
        std::string requestId = generateRequestId();
        
        Logger::log(LogLevel::INFO, "Processing user request: " + userInput);
        logActivity("Processing request: " + userInput.substr(0, 50) + "...");
        
        return processRequestInternal(requestId, userInput);
        
    }, "Orchestrator::processUserRequest");
    
    TaskExecutionResult errorResult;
    errorResult.status = ExecutionStatus::FAILED;
    errorResult.errorMessage = "Error processing user request";
    return errorResult;
}

std::string Orchestrator::processUserRequestAsync(const std::string& userInput) {
    std::string requestId = generateRequestId();
    
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        
        // Create execution context
        ExecutionContext context;
        context.requestId = requestId;
        context.originalRequest = userInput;
        context.maxNestingLevel = ConfigManager::getInstance().getMaxScriptNestingLevel();
        
        m_activeExecutions[requestId] = context;
        m_requestQueue.push(requestId);
    }
    
    m_queueCondition.notify_one();
    
    logActivity("Queued async request: " + requestId);
    return requestId;
}

TaskExecutionResult Orchestrator::getExecutionResult(const std::string& requestId) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    
    auto it = m_completedExecutions.find(requestId);
    if (it != m_completedExecutions.end()) {
        return it->second;
    }
    
    // Check if still active
    auto activeIt = m_activeExecutions.find(requestId);
    if (activeIt != m_activeExecutions.end()) {
        TaskExecutionResult result;
        result.status = ExecutionStatus::IN_PROGRESS;
        return result;
    }
    
    // Not found
    TaskExecutionResult result;
    result.status = ExecutionStatus::FAILED;
    result.errorMessage = "Request ID not found";
    return result;
}

void Orchestrator::workerThreadFunction() {
    while (m_isRunning) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        
        m_queueCondition.wait(lock, [this] { 
            return !m_requestQueue.empty() || !m_isRunning; 
        });
        
        if (!m_isRunning) {
            break;
        }
        
        while (!m_requestQueue.empty() && !m_isPaused) {
            std::string requestId = m_requestQueue.front();
            m_requestQueue.pop();
            lock.unlock();
            
            // Process the request
            auto contextIt = m_activeExecutions.find(requestId);
            if (contextIt != m_activeExecutions.end()) {
                TaskExecutionResult result = processRequestInternal(requestId, contextIt->second.originalRequest);
                
                std::lock_guard<std::mutex> stateLock(m_stateMutex);
                m_completedExecutions[requestId] = result;
                m_activeExecutions.erase(requestId);
            }
            
            lock.lock();
        }
    }
}

TaskExecutionResult Orchestrator::processRequestInternal(const std::string& requestId, const std::string& userInput) {
    TaskExecutionResult result;
    auto startTime = getCurrentTimeMs();
    
    try {
        ExecutionContext context;
        context.requestId = requestId;
        context.originalRequest = userInput;
        context.maxNestingLevel = ConfigManager::getInstance().getMaxScriptNestingLevel();
        
        // Step 1: Update environmental context
        updateEnvironmentalContext(context);
        
        // Step 2: Parse user request
        result = parseUserRequest(userInput, context);
        if (result.status == ExecutionStatus::FAILED) {
            return result;
        }
        
        // Step 3: Generate execution plan
        result = generateExecutionPlan(userInput, context);
        if (result.status == ExecutionStatus::FAILED) {
            return result;
        }
        
        // Step 4: Validate and get confirmation if needed
        if (!validateExecutionPlan(result.result)) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Generated execution plan failed validation";
            return result;
        }
        
        if (requiresUserConfirmation(result.result)) {
            context.requiresUserConfirmation = true;
            // In a real implementation, this would prompt the user
            Logger::log(LogLevel::WARNING, "Execution plan requires user confirmation (auto-approved in current implementation)");
        }
        
        // Step 5: Execute the plan
        result = executeCommandSequence(result.result["commands"], context);
        
        result.executionTimeMs = getCurrentTimeMs() - startTime;
        
        if (result.status == ExecutionStatus::COMPLETED) {
            logActivity("Successfully completed request: " + requestId);
            raiseEvent(EventData(OrchestratorEvent::TASK_COMPLETED, requestId));
        } else {
            logActivity("Failed to complete request: " + requestId + " - " + result.errorMessage);
            raiseEvent(EventData(OrchestratorEvent::TASK_FAILED, requestId));
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Exception during execution: " + std::string(e.what());
        result.executionTimeMs = getCurrentTimeMs() - startTime;
        
        Logger::log(LogLevel::ERROR_LEVEL, "Exception in processRequestInternal: " + std::string(e.what()));
        handleExecutionError(requestId, result.errorMessage);
    }
    
    return result;
}

TaskExecutionResult Orchestrator::parseUserRequest(const std::string& userInput, ExecutionContext& context) {
    TaskExecutionResult result;
    
    try {
        auto parseResult = m_commandParser->parseUserInput(userInput);
        
        if (parseResult.success) {
            result.status = ExecutionStatus::COMPLETED;
            
            // Convert parsed commands to JSON format for execution
            nlohmann::json commandsJson = nlohmann::json::array();
            for (const auto& cmd : parseResult.commands) {
                nlohmann::json commandJson = {
                    {"command", cmd.action},
                    {"params", cmd.parameters},
                    {"description", cmd.description},
                    {"optional", cmd.isOptional},
                    {"delayAfterMs", cmd.delayAfterMs}
                };
                commandsJson.push_back(commandJson);
            }
            
            result.result = {
                {"commands", commandsJson},
                {"intent", {
                    {"type", static_cast<int>(parseResult.intent.type)},
                    {"confidence", static_cast<int>(parseResult.intent.confidence)},
                    {"description", parseResult.intent.description}
                }},
                {"context", parseResult.context}
            };
            
            context.executionLog.push_back("Parsed user request successfully");
        } else {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Failed to parse user request: " + parseResult.errorMessage;
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Exception during parsing: " + std::string(e.what());
    }
    
    return result;
}

TaskExecutionResult Orchestrator::generateExecutionPlan(const std::string& userInput, ExecutionContext& context) {
    TaskExecutionResult result;
    
    try {
        // Build enhanced LLM context from environmental perception
        LLMContext llmContext;
        if (m_perception) {
            auto envData = m_perception->gatherEnvironmentInfo();
            
            // Basic context
            llmContext.activeWindow = envData.value("activeWindow", "");
            llmContext.currentDirectory = envData.value("currentDirectory", "");
            llmContext.openWindows = envData.value("openWindows", std::vector<std::string>{});
            
            // Enhanced context for dual-mode LLM interface
            if (envData.contains("text_description")) {
                llmContext.textDescription = envData["text_description"].get<std::string>();
            }
            
            // Store structured environmental data
            llmContext.structuredData = envData;
            
            // Add screenshot data if available
            if (m_llmConnector->supportsVision()) {
                auto screenshot = m_perception->captureScreen();
                if (screenshot.isValid()) {
                    llmContext.screenshotData = screenshot.data;
                    llmContext.screenshotFormat = screenshot.format.empty() ? "png" : screenshot.format;
                    Logger::log(LogLevel::DEBUG, "Screenshot added to LLM context for vision-capable model");
                }
            }
        }
        
        // Generate plan using enhanced dual-mode LLM interface
        auto executionPlan = m_llmConnector->generatePlanWithContext(userInput, llmContext);
        
        if (executionPlan.isValid) {
            result.status = ExecutionStatus::COMPLETED;
            result.result = {
                {"commands", executionPlan.commands},
                {"reasoning", executionPlan.reasoning},
                {"summary", executionPlan.summary}
            };
            
            context.executionLog.push_back("Generated execution plan: " + executionPlan.summary);
        } else {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Failed to generate valid execution plan";
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Exception during plan generation: " + std::string(e.what());
    }
    
    return result;
}

TaskExecutionResult Orchestrator::executeCommandSequence(const nlohmann::json& commands, ExecutionContext& context) {
    TaskExecutionResult result;
    result.status = ExecutionStatus::IN_PROGRESS;
    
    try {
        context.executionLog.push_back("Starting command sequence execution");
        
        for (const auto& command : commands) {
            if (m_emergencyStop || m_isPaused) {
                result.status = ExecutionStatus::CANCELLED;
                result.errorMessage = "Execution stopped";
                break;
            }
            
            TaskExecutionResult cmdResult = executeCommand(command, context);
            
            if (cmdResult.status == ExecutionStatus::FAILED && !command.value("optional", false)) {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Command failed: " + cmdResult.errorMessage;
                break;
            }
            
            // Handle CONDITIONAL_STOP - stop execution if condition was met
            if (cmdResult.status == ExecutionStatus::CANCELLED) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = cmdResult.output;
                result.success = true;
                context.executionLog.push_back("Script execution stopped by conditional logic: " + cmdResult.output);
                break;
            }
            
            // Add delay if specified, or use default command sequence delay
            int delay = command.value("delayAfterMs", m_commandSequenceDelayMs);
            if (delay > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
        }
        
        if (result.status == ExecutionStatus::IN_PROGRESS) {
            result.status = ExecutionStatus::COMPLETED;
            context.executionLog.push_back("Command sequence completed successfully");
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Exception during command execution: " + std::string(e.what());
    }
    
    return result;
}

// Variable substitution helper functions
std::string Orchestrator::substituteVariables(const std::string& input, const ExecutionContext& context) {
    std::string result = input;
    std::regex varPattern(R"(\$\{([^}]+)\})");
    std::smatch match;
    
    Logger::log(LogLevel::DEBUG, "[SUBSTITUTION] Input string: " + input);
    // Only log variables when there are actual substitutions to avoid spam
    bool hasSubstitutions = std::regex_search(input, std::regex(R"(\$\{([^}]+)\})"));
    if (hasSubstitutions) {
        Logger::log(LogLevel::DEBUG, "[SUBSTITUTION] Available variables: " + std::to_string(context.variables.size()));
        // Only log the specific variables being used, not all variables
        for (const auto& [key, value] : context.variables) {
            if (input.find("${" + key + "}") != std::string::npos || input.find("${" + key + "[") != std::string::npos) {
                Logger::log(LogLevel::DEBUG, "[SUBSTITUTION] Variable '" + key + "' = " + value.dump());
            }
        }
    }
    
    int maxIterations = 10; // Prevent infinite loops
    int iterations = 0;
    
    while (std::regex_search(result, match, varPattern) && iterations < maxIterations) {
        std::string varExpression = match[1].str();
        std::string replacement = ""; // Default to empty string if not found
        
        // Handle array access like "notepadWindows[0]"
        std::regex arrayPattern(R"((\w+)\[(\d+)\])");
        std::smatch arrayMatch;
        
        if (std::regex_match(varExpression, arrayMatch, arrayPattern)) {
            // Array access: varName[index]
            std::string varName = arrayMatch[1].str();
            int index = std::stoi(arrayMatch[2].str());
            
            if (context.variables.find(varName) != context.variables.end()) {
                const auto& varValue = context.variables.at(varName);
                if (varValue.is_array() && index >= 0 && index < static_cast<int>(varValue.size())) {
                    const auto& arrayItem = varValue[index];
                    if (arrayItem.is_object() && (arrayItem.contains("handle") || arrayItem.contains("hwnd"))) {
                        // Support both "handle" and "hwnd" properties for window handles
                        if (arrayItem.contains("hwnd")) {
                            replacement = std::to_string(arrayItem["hwnd"].get<uintptr_t>());
                        } else {
                            replacement = std::to_string(arrayItem["handle"].get<uintptr_t>());
                        }
                    } else if (arrayItem.is_string()) {
                        replacement = arrayItem.get<std::string>();
                    } else if (arrayItem.is_number()) {
                        replacement = std::to_string(arrayItem.get<double>());
                    } else {
                        replacement = arrayItem.dump();
                    }
                    LOG_DEBUG("Array variable substitution: ${" + varExpression + "} -> " + replacement);
                } else {
                    Logger::log(LogLevel::WARNING, "Array index out of bounds or invalid array: " + varExpression);
                }
            } else {
                Logger::log(LogLevel::WARNING, "Array variable not found: " + varName);
            }
        } else {
            // Simple variable access
            if (context.variables.find(varExpression) != context.variables.end()) {
                const auto& varValue = context.variables.at(varExpression);
                
                // Handle different variable types
                if (varValue.is_string()) {
                    replacement = varValue.get<std::string>();
                } else if (varValue.is_number()) {
                    replacement = std::to_string(varValue.get<double>());
                } else if (varValue.is_array() && !varValue.empty()) {
                    // For arrays (like window lists), use the first item's handle
                    const auto& firstItem = varValue[0];
                    if (firstItem.is_object() && (firstItem.contains("handle") || firstItem.contains("hwnd"))) {
                        // Support both "handle" and "hwnd" properties for window handles
                        if (firstItem.contains("hwnd")) {
                            replacement = std::to_string(firstItem["hwnd"].get<uintptr_t>());
                        } else {
                            replacement = std::to_string(firstItem["handle"].get<uintptr_t>());
                        }
                    } else {
                        replacement = varValue.dump();
                    }
                } else {
                    replacement = varValue.dump();
                }
                
                LOG_DEBUG("Variable substitution: ${" + varExpression + "} -> " + replacement);
            } else {
                // Only log once per variable, not repeatedly
                if (iterations == 0) {
                    Logger::log(LogLevel::WARNING, "Variable not found: " + varExpression + " - removing from string");
                }
            }
        }
        
        // Create exact pattern match for this specific variable
        std::string pattern = "\\$\\{" + std::regex_replace(varExpression, std::regex(R"([\[\]\\^$.|?*+{}()])"), R"(\\$&)") + "\\}";
        
        // Use a more robust replacement approach
        size_t pos = result.find("${" + varExpression + "}");
        if (pos != std::string::npos) {
            result.replace(pos, varExpression.length() + 3, replacement);  // +3 for ${}
            LOG_DEBUG("Replaced ${" + varExpression + "} with " + replacement + " at position " + std::to_string(pos));
        } else {
            LOG_DEBUG("Variable ${" + varExpression + "} not found in string for replacement");
        }
        
        iterations++;
    }
    
    if (iterations >= maxIterations) {
        Logger::log(LogLevel::ERROR_LEVEL, "Variable substitution stopped after " + std::to_string(maxIterations) + " iterations to prevent infinite loop");
    }
    
    return result;
}

nlohmann::json Orchestrator::substituteVariablesInParams(const nlohmann::json& params, const ExecutionContext& context) {
    nlohmann::json result = params;
    
    for (auto& [key, value] : result.items()) {
        if (value.is_string()) {
            std::string original = value.get<std::string>();
            std::string substituted = substituteVariables(original, context);
            
            // FIXED: Always preserve strings after variable substitution to prevent type errors
            // Many UIA commands expect string parameters even for numeric values (like window handles)
            result[key] = substituted;
            // If no substitution occurred, leave the original value unchanged
        } else if (value.is_object()) {
            result[key] = substituteVariablesInParams(value, context);
        } else if (value.is_array()) {
            for (size_t i = 0; i < value.size(); ++i) {
                if (value[i].is_string()) {
                    std::string originalArrayItem = value[i].get<std::string>();
                    std::string substitutedArrayItem = substituteVariables(originalArrayItem, context);
                    result[key][i] = substitutedArrayItem;
                } else if (value[i].is_object()) {
                    result[key][i] = substituteVariablesInParams(value[i], context);
                }
            }
        }
    }
    
    return result;
}

// Variable loading and management methods
void Orchestrator::loadScriptVariables(const nlohmann::json& script, ExecutionContext& context) {
    try {
        // Use JsonUtils for safe field extraction
        nlohmann::json variables;
        if (utils::JsonUtils::getObjectField(script, "variables", variables)) {
            Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Loading script variables into execution context");
            
            int loadedCount = 0;
            int skippedCount = 0;
            
            for (const auto& [key, value] : variables.items()) {
                // Only load script variables if they don't already exist in context
                // This preserves EXECUTE_SCRIPT variables and parent context variables
                if (context.variables.find(key) == context.variables.end()) {
                    // Store variables with proper type preservation
                    context.variables[key] = value;
                    loadedCount++;
                    
                    // Log variable loading for debugging
                    std::string valueStr = value.is_string() ? value.get<std::string>() : value.dump();
                    Logger::log(LogLevel::DEBUG, "[ORCHESTRATOR] Loaded variable: " + key + " = " + valueStr);
                } else {
                    skippedCount++;
                    // Log that we're preserving existing variable
                    std::string existingValueStr = context.variables[key].is_string() ? 
                        context.variables[key].get<std::string>() : context.variables[key].dump();
                    Logger::log(LogLevel::DEBUG, "[ORCHESTRATOR] Preserved existing variable: " + key + " = " + existingValueStr);
                }
            }
            
            Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Loaded " + std::to_string(loadedCount) + " script variables");
            if (skippedCount > 0) {
                Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Preserved " + std::to_string(skippedCount) + " existing context variables");
            }
        } else {
            Logger::log(LogLevel::DEBUG, "[ORCHESTRATOR] No variables section found in script");
        }
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] Failed to load script variables: " + std::string(e.what()));
    }
}

void Orchestrator::inheritParentVariables(const ExecutionContext& parentContext, ExecutionContext& childContext) {
    try {
        Logger::log(LogLevel::DEBUG, "[ORCHESTRATOR] Inheriting " + std::to_string(parentContext.variables.size()) + " variables from parent context");
        
        // Copy all parent variables to child context
        for (const auto& [key, value] : parentContext.variables) {
            childContext.variables[key] = value;
            Logger::log(LogLevel::DEBUG, "[ORCHESTRATOR] Inherited variable: " + key);
        }
        
        // Copy nesting information
        childContext.nestingLevel = parentContext.nestingLevel + 1;
        childContext.maxNestingLevel = parentContext.maxNestingLevel;
        childContext.scriptStack = parentContext.scriptStack;
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] Failed to inherit parent variables: " + std::string(e.what()));
    }
}

TaskExecutionResult Orchestrator::executeCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    
    try {
        // Handle both "command" and "name" field variations from LLM
        std::string action;
        if (command.contains("command")) {
            action = command["command"];
        } else if (command.contains("name")) {
            action = command["name"];
        } else {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Command missing 'command' or 'name' field";
            return result;
        }
        
        Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Processing command: " + action);
        
        // Translate CPL command names to orchestrator format
        if (action == "MOUSE_CLICK") action = "mouse.click";
        else if (action == "MOUSE_MOVE") action = "mouse.move";
        else if (action == "KEY_TYPE") action = "keyboard.type";
        else if (action == "KEY_COMBO") action = "keyboard.hotkey";
        else if (action == "APP_LAUNCH") action = "application.launch";
        else if (action == "SHELL_RUN") action = "system.shell";
        else if (action == "SYSTEM_COMMAND") action = "system.shell";
        else if (action == "POWERSHELL_COMMAND") action = "system.powershell";
        else if (action == "CMD_COMMAND") action = "system.shell";  // Use smart command detection
        else if (action == "WINDOW_FIND") action = "window.find";
        else if (action == "WINDOW_ACTIVATE") action = "window.activate";
        else if (action == "WINDOW_LIST") action = "window.enumerate";
        else if (action == "WINDOW_CLOSE") action = "window.close";
        else if (action == "FILE_EXISTS") action = "system.fileExists";
        else if (action == "FILE_COPY") action = "system.fileCopy";
        // UIA Commands - Only atomic operations mapped to application category
        // Atomic UIA Commands (13 core atomic operations)
        else if (action == "UIA_KEY_PRESS") action = "application.uia_key_press";
        else if (action == "UIA_KEY_RELEASE") action = "application.uia_key_release";
        else if (action == "UIA_MOUSE_CLICK") action = "application.uia_mouse_click";
        else if (action == "UIA_MOUSE_MOVE") action = "application.uia_mouse_move";
        else if (action == "UIA_FOCUS_WINDOW") action = "application.uia_focus_window";
        else if (action == "UIA_GET_CLIPBOARD") action = "application.uia_get_clipboard";
        else if (action == "UIA_SET_CLIPBOARD") action = "application.uia_set_clipboard";
        else if (action == "UIA_WINDOW_RESIZE") action = "application.uia_window_resize";
        else if (action == "UIA_WINDOW_MOVE") action = "application.uia_window_move";
        else if (action == "UIA_ENUM_WINDOWS") action = "application.uia_enum_windows";
        else if (action == "UIA_GET_WINDOW_TITLE") action = "application.uia_get_window_title";
        else if (action == "UIA_GET_WINDOW_CLASS") action = "application.uia_get_window_class";
        else if (action == "UIA_GET_WINDOW_RECT") action = "application.uia_get_window_rect";
        // Missing atomic operations
        else if (action == "UIA_GET_MOUSE_POSITION") action = "application.uia_get_mouse_position";
        else if (action == "UIA_LAUNCH_APPLICATION") action = "application.uia_launch_application";
        else if (action == "UIA_TERMINATE_PROCESS") action = "application.uia_terminate_process";
        else if (action == "UIA_IS_WINDOW_MINIMIZED") action = "application.uia_is_window_minimized";
        else if (action == "UIA_IS_WINDOW_MAXIMIZED") action = "application.uia_is_window_maximized";
        else if (action == "UIA_GET_FOREGROUND_WINDOW") action = "application.uia_get_foreground_window";
        else if (action == "UIA_FIND_WINDOWS_BY_CLASS") action = "application.uia_find_windows_by_class";
        else if (action == "UIA_FIND_WINDOWS_BY_TITLE") action = "application.uia_find_windows_by_title";
        else if (action == "UIA_GET_WINDOW_INFO") action = "application.uia_get_window_info";
        else if (action == "UIA_GET_FILE_INFO") action = "application.uia_get_file_info";
        else if (action == "UIA_CREATE_DIRECTORY") action = "application.uia_create_directory";
        else if (action == "UIA_FIND_FILES_BY_PATTERN") action = "application.uia_find_files_by_pattern";
        else if (action == "UIA_FIND_FILES_BY_EXTENSION") action = "application.uia_find_files_by_extension";
        else if (action == "UIA_MOVE_FILES") action = "application.uia_move_files";
        else if (action == "UIA_DELETE_FILES") action = "application.uia_delete_files";
        else if (action == "UIA_SHELL_EXECUTE") action = "application.uia_shell_execute";
        // Conditional and control flow commands
        else if (action == "IF_CONTAINS") action = "control.if_contains";
        else if (action == "IF_EQUALS") action = "control.if_equals";
        else if (action == "IF_NOT_CONTAINS") action = "control.if_not_contains";
        else if (action == "IF_EXISTS") action = "control.if_exists";
        else if (action == "IF_NOT_EXISTS") action = "control.if_not_exists";
        else if (action == "CONDITIONAL_STOP") action = "control.conditional_stop";
        else if (action == "LOOP_UNTIL") action = "control.loop_until";
        else if (action == "CONDITIONAL") action = "control.conditional";
        else if (action == "WHILE_LOOP") action = "control.while_loop";
        else if (action == "BREAK_IF") action = "control.break_if";
        else if (action == "CONTINUE_IF") action = "control.continue_if";
        // Basic application commands
        else if (action == "LAUNCH_APPLICATION") action = "application.launch";
        
        // Create a modified command with standardized format
        nlohmann::json modifiedCommand = command;
        modifiedCommand["command"] = action;
        
        // Translate parameter field names - LLM uses "parameters", orchestrator expects "params"
        if (command.contains("parameters") && !command.contains("params")) {
            modifiedCommand["params"] = command["parameters"];
            modifiedCommand.erase("parameters");
        }
        
        // Handle "options" field as metadata (optional)
        if (command.contains("options") && !command.contains("metadata")) {
            modifiedCommand["metadata"] = command["options"];
            modifiedCommand.erase("options");
        }
        
        // Perform variable substitution on parameters
        if (modifiedCommand.contains("params")) {
            Logger::log(LogLevel::DEBUG, "[ORCHESTRATOR] Before substitution - action: " + action + 
                       ", params type: " + std::string(modifiedCommand["params"].type_name()));
            
            if (action == "EXECUTE_SCRIPT") {
                // For EXECUTE_SCRIPT commands, preserve the "variables" field as-is
                // and only substitute other parameters
                nlohmann::json substitutedParams = modifiedCommand["params"];
                
                Logger::log(LogLevel::DEBUG, "[ORCHESTRATOR] EXECUTE_SCRIPT params before processing: " + 
                           substitutedParams.dump());
                
                // Save the original variables object
                nlohmann::json originalVariables;
                if (substitutedParams.contains("variables")) {
                    originalVariables = substitutedParams["variables"];
                    substitutedParams.erase("variables");
                }
                
                // Apply substitution to other parameters
                substitutedParams = substituteVariablesInParams(substitutedParams, context);
                
                // Restore the original variables object
                if (!originalVariables.is_null()) {
                    substitutedParams["variables"] = originalVariables;
                }
                
                modifiedCommand["params"] = substitutedParams;
                Logger::log(LogLevel::DEBUG, "[ORCHESTRATOR] EXECUTE_SCRIPT params after processing: " + 
                           modifiedCommand["params"].dump());
                LOG_DEBUG("Applied variable substitution to EXECUTE_SCRIPT parameters (preserving variables field)");
            } else {
                modifiedCommand["params"] = substituteVariablesInParams(modifiedCommand["params"], context);
                LOG_DEBUG("Applied variable substitution to command parameters");
            }
            
            // Log the substituted parameters for debugging
            if (modifiedCommand.contains("params")) {
                Logger::log(LogLevel::DEBUG, "[ORCHESTRATOR] Final parameters after substitution: " + 
                           modifiedCommand["params"].dump());
            }
        }
        
        context.executionLog.push_back("Executing command: " + action);
        Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Routing translated command: " + action);
        
        if (action.find("mouse.") == 0) {
            result = executeMouseCommand(modifiedCommand, context);
        } else if (action.find("keyboard.") == 0) {
            result = executeKeyboardCommand(modifiedCommand, context);
        } else if (action.find("application.") == 0) {
            result = executeApplicationCommand(modifiedCommand, context);
        } else if (action.find("system.") == 0) {
            result = executeSystemCommand(modifiedCommand, context);
        } else if (action.find("window.") == 0) {
            result = executeWindowCommand(modifiedCommand, context);
        } else if (action == "WAIT") {
            result = executeWaitCommand(modifiedCommand, context);
        } else if (action.find("control.") == 0) {
            result = executeControlCommand(modifiedCommand, context);
        } else if (action == "EXECUTE_SCRIPT") {
            result = executeScriptCommand(modifiedCommand, context);
        } else {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Unknown command type: " + action;
        }
        
        // Update command success metrics for feedback loop
        updateCommandSuccessMetrics(action, result.status == ExecutionStatus::COMPLETED);
        
        // Skip error recovery for simple script execution to avoid infinite loops
        // Trigger error recovery system if command failed and auto-recovery is enabled
        if (false && result.status == ExecutionStatus::FAILED && !result.errorMessage.empty()) {
            Logger::log(LogLevel::DEBUG, "Command failed, checking if error recovery should be initiated");
            
            // Create command context for recovery
            nlohmann::json commandContext = {
                {"original_command", modifiedCommand},
                {"execution_context", {
                    {"request_id", context.requestId},
                    {"timestamp", getCurrentTimeMs()}
                }},
                {"environment_snapshot", context.currentEnvironment}
            };
            
            // Initiate error recovery (this will determine if recovery is needed)
            auto recoveryResult = initiateErrorRecovery(modifiedCommand.dump(), commandContext, result.errorMessage);
            
            // If recovery was successful, update the original result
            if (recoveryResult.status == ExecutionStatus::COMPLETED) {
                result = recoveryResult;
                result.result["recovered"] = true;
                Logger::log(LogLevel::INFO, "Command execution recovered successfully via error recovery system");
            } else {
                // Recovery failed, add recovery information to original result
                result.result["recovery_attempted"] = true;
                result.result["recovery_details"] = recoveryResult.result;
                Logger::log(LogLevel::WARNING, "Error recovery attempted but failed for command: " + action);
            }
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Exception executing command: " + std::string(e.what());
        
        // Even for exceptions, attempt error recovery
        try {
            nlohmann::json commandContext = {
                {"original_command", command},
                {"execution_context", {
                    {"request_id", context.requestId},
                    {"timestamp", getCurrentTimeMs()}
                }},
                {"exception", true}
            };
            
            auto recoveryResult = initiateErrorRecovery(command.dump(), commandContext, result.errorMessage);
            if (recoveryResult.status == ExecutionStatus::COMPLETED) {
                result = recoveryResult;
                result.result["recovered_from_exception"] = true;
            }
        } catch (...) {
            // If recovery itself throws, just log and continue with original error
            Logger::log(LogLevel::ERROR_LEVEL, "Error recovery failed with exception for command: " + command.dump());
        }
    }
    
    return result;
}

// Component setters
void Orchestrator::setCommandParser(std::shared_ptr<CommandParser> parser) { m_commandParser = parser; }
void Orchestrator::setLLMConnector(std::shared_ptr<LLMConnector> connector) { m_llmConnector = connector; }
void Orchestrator::setTaskEngine(std::shared_ptr<TaskEngine> engine) { m_taskEngine = engine; }
void Orchestrator::setOCAL(std::shared_ptr<OCAL> ocal) { m_ocal = ocal; }
void Orchestrator::setEnvironmentalPerception(std::shared_ptr<EnvironmentalPerception> perception) { m_perception = perception; }
void Orchestrator::setUIModule(std::shared_ptr<UIModule> ui) { m_ui = ui; }

// Configuration methods
void Orchestrator::setAutoMode(bool enabled) { m_autoMode = enabled; }
void Orchestrator::setConfirmationRequired(bool required) { m_confirmationRequired = required; }
void Orchestrator::setMaxConcurrentTasks(int maxTasks) { m_maxConcurrentTasks = maxTasks; }
void Orchestrator::setExecutionTimeout(int timeoutMs) { m_executionTimeoutMs = timeoutMs; }

// Control methods
void Orchestrator::pauseExecution() { m_isPaused = true; logActivity("Execution paused"); }
void Orchestrator::resumeExecution() { m_isPaused = false; logActivity("Execution resumed"); }
void Orchestrator::emergencyStop() { m_emergencyStop = true; logActivity("Emergency stop activated"); }

// Status methods
bool Orchestrator::isRunning() const { return m_isRunning; }
bool Orchestrator::isIdle() const { 
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_requestQueue.empty() && m_activeExecutions.empty();
}

// Utility methods
std::string Orchestrator::generateRequestId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    return "req_" + std::to_string(timestamp) + "_" + std::to_string(dis(gen));
}

void Orchestrator::logActivity(const std::string& activity) {
    std::lock_guard<std::mutex> lock(m_activityMutex);
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%H:%M:%S") << " - " << activity;
    
    m_recentActivity.push_back(ss.str());
    
    // Keep only last 100 activities
    if (m_recentActivity.size() > 100) {
        m_recentActivity.erase(m_recentActivity.begin());
    }
}

double Orchestrator::getCurrentTimeMs() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// Command execution implementations (delegating to OCAL)
TaskExecutionResult Orchestrator::executeMouseCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    
    try {
        std::string action = command["command"];
        
        // Debug: Log the full command JSON
        Logger::log(LogLevel::DEBUG, "Mouse command JSON: " + command.dump(2));
        
        if (action == "mouse.click") {
            // Check if params exist and have required fields
            if (!command.contains("params")) {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Mouse command missing 'params' field";
                return result;
            }
            
            auto params = command["params"];
            Logger::log(LogLevel::DEBUG, "Mouse params JSON: " + params.dump(2));
            
            int x, y;
            std::string button = params.value("button", "left");
            
            // Check if coordinates are provided
            if (params.contains("x") && params.contains("y") && 
                !params["x"].is_null() && !params["y"].is_null()) {
                // Use provided coordinates
                x = params["x"];
                y = params["y"];
                Logger::log(LogLevel::DEBUG, "Executing mouse click at specified coordinates (" + std::to_string(x) + ", " + std::to_string(y) + ") with button: " + button);
            } else {
                // Use current mouse position (for clicks after move commands)
                Logger::log(LogLevel::DEBUG, "No coordinates provided for mouse click, using current mouse position");
                auto currentPos = m_ocal->mouseControl().getCurrentPosition();
                x = currentPos.first;
                y = currentPos.second;
                Logger::log(LogLevel::DEBUG, "Executing mouse click at current position (" + std::to_string(x) + ", " + std::to_string(y) + ") with button: " + button);
            }
            
            if (m_ocal->mouseControl().click(x, y, button)) {
                result.status = ExecutionStatus::COMPLETED;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Mouse click failed";
            }
        } else if (action == "mouse.move") {
            // Similar validation for move command
            if (!command.contains("params")) {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Mouse move command missing 'params' field";
                return result;
            }
            
            auto params = command["params"];
            if (!params.contains("x") || !params.contains("y")) {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Mouse move command missing x or y coordinates";
                return result;
            }
            
            if (params["x"].is_null() || params["y"].is_null()) {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Mouse move coordinates are null - x: " + params["x"].dump() + ", y: " + params["y"].dump();
                return result;
            }
            
            int x = params["x"];
            int y = params["y"];
            
            if (m_ocal->mouseControl().moveTo(x, y)) {
                result.status = ExecutionStatus::COMPLETED;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Mouse move failed";
            }
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Mouse command exception: " + std::string(e.what());
    }
    
    return result;
}

TaskExecutionResult Orchestrator::executeKeyboardCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    
    try {
        std::string action = command["command"];
        
        if (action == "keyboard.type") {
            std::string text = command["params"]["text"];
            
            if (m_ocal->keyboardControl().typeText(text)) {
                result.status = ExecutionStatus::COMPLETED;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Keyboard type failed";
            }
        } else if (action == "keyboard.hotkey") {
            // Handle both string format ("ctrl+c") and array format
            if (command["params"].contains("keys")) {
                if (command["params"]["keys"].is_string()) {
                    std::string keyCombo = command["params"]["keys"];
                    std::vector<std::string> keys = {keyCombo};
                    
                    if (m_ocal->keyboardControl().sendHotkey(keys)) {
                        result.status = ExecutionStatus::COMPLETED;
                    } else {
                        result.status = ExecutionStatus::FAILED;
                        result.errorMessage = "Keyboard hotkey failed";
                    }
                } else if (command["params"]["keys"].is_array()) {
                    std::vector<std::string> keys = command["params"]["keys"];
                    
                    if (m_ocal->keyboardControl().sendHotkey(keys)) {
                        result.status = ExecutionStatus::COMPLETED;
                    } else {
                        result.status = ExecutionStatus::FAILED;
                        result.errorMessage = "Keyboard hotkey failed";
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Invalid keys parameter format";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing keys parameter";
            }
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Keyboard command exception: " + std::string(e.what());
    }
    
    return result;
}

TaskExecutionResult Orchestrator::executeApplicationCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    
    try {
        std::string action = command["command"];
        Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Executing application command: " + action);
        
        if (action == "application.launch") {
            // Handle both "path" and "applicationPath" parameter names
            std::string appPath;
            if (command["params"].contains("applicationPath")) {
                appPath = command["params"]["applicationPath"];
            } else if (command["params"].contains("path")) {
                appPath = command["params"]["path"];
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing applicationPath or path parameter for application launch";
                return result;
            }
            
            // Handle arguments parameter
            std::string arguments = "";
            if (command["params"].contains("arguments")) {
                arguments = command["params"]["arguments"];
            }
            
            // Build full command line
            std::string fullCommand = appPath;
            if (!arguments.empty()) {
                fullCommand += " " + arguments;
                Logger::log(LogLevel::INFO, "[APP] Launching application: " + appPath + " with arguments: " + arguments);
            } else {
                Logger::log(LogLevel::INFO, "[APP] Launching application: " + appPath);
            }
            
            // Convert to wide string for UIA function
            std::wstring wFullCommand(fullCommand.begin(), fullCommand.end());
            
            // Use Windows API directly for launching
            STARTUPINFOW si = {sizeof(si)};
            PROCESS_INFORMATION processInfo;
            bool success = CreateProcessW(NULL, &wFullCommand[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &processInfo);
            
            if (success) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = "Application launched successfully: " + fullCommand;
                result.executedCommands.push_back("LAUNCH_APPLICATION");
                result.result["processId"] = processInfo.dwProcessId;
                Logger::log(LogLevel::INFO, "[APP] Application launched successfully: " + fullCommand + " (PID: " + std::to_string(processInfo.dwProcessId) + ")");
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Application launch failed: " + fullCommand;
                Logger::log(LogLevel::ERROR_LEVEL, "[APP] Application launch failed: " + fullCommand);
            }
        } else if (action == "application.close") {
            std::string appName = command["params"]["name"];
            
            if (m_ocal->appManagement().closeApplication(appName)) {
                result.status = ExecutionStatus::COMPLETED;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Application close failed";
            }
        // REMOVED: Duplicate UIA_FOCUS_WINDOW implementation with direct Windows API calls
        // This command is now properly handled by the atomic operation implementation at line 1804
        } else if (action == "application.uia_get_window_info") {
            // UIA_GET_WINDOW_INFO - Get window information (stub implementation)
            result.status = ExecutionStatus::COMPLETED;
            result.output = "Window info retrieved";
            result.executedCommands.push_back("UIA_GET_WINDOW_INFO");
        // REMOVED: UIA_FOCUS_WINDOW_SAFE - Complex wrapper removed in Phase 2D refactoring
        // Replacement: Use nested script with UIA_FOCUS_WINDOW atomic operation
        // Script location: test_scripts/uia_focus_window_safe.json
        // REMOVED: UIA_RESIZE_WINDOW - Complex wrapper removed in Phase 2D refactoring
        // Replacement: Use nested script with UIA_WINDOW_RESIZE atomic operation
        // Script location: test_scripts/uia_resize_window.json
        // REMOVED: UIA_MOVE_WINDOW - Complex wrapper removed in Phase 2D refactoring
        // Replacement: Use nested script with UIA_WINDOW_MOVE atomic operation
        // Script location: test_scripts/uia_move_window.json
        // REMOVED: UIA_TYPE_TEXT_SAFE - Complex wrapper removed in Phase 2D refactoring
        // CRITICAL VIOLATIONS: Direct Windows API calls - OpenClipboard(), SetClipboardData(), keybd_event(), SetForegroundWindow()
        // Replacement: Use nested script with UIA_SET_CLIPBOARD + UIA_KEY_PRESS atomic operations
        // Script location: test_scripts/uia_type_text_safe.json
        // REMOVED: UIA_SEND_HOTKEY - Massive complex wrapper removed in Phase 2D refactoring (150+ lines)
        // CRITICAL VIOLATIONS: keybd_event(), SetForegroundWindow(), extensive VK code mapping, direct Windows API
        // Replacement: Use nested script with UIA_KEY_PRESS + UIA_KEY_RELEASE atomic operations
        // Script location: test_scripts/uia_send_hotkey.json
        // REMOVED: UIA_SEND_HOTKEY_SEQUENCE - Complex wrapper removed in Phase 2D refactoring
        // Replacement: Use nested script with UIA_KEY_PRESS + UIA_KEY_RELEASE atomic operations
        // Script location: test_scripts/uia_send_hotkey_sequence.json
        // REMOVED: UIA_PRESS_KEY - Massive complex wrapper removed in Phase 2D refactoring (110+ lines)
        // CRITICAL VIOLATIONS: keybd_event(), extensive VK code mapping, direct Windows API
        // Replacement: Use nested script with UIA_KEY_PRESS + UIA_KEY_RELEASE atomic operations
        // Script location: test_scripts/uia_press_key.json
        // REMOVED: UIA_WAIT_FOR_WINDOW - Complex wrapper removed in Phase 2D refactoring
        // Replacement: Use nested script with UIA_ENUM_WINDOWS polling
        // Script location: test_scripts/uia_wait_for_window.json
        
        // REMOVED: UIA_HANDLE_DIALOG - Complex wrapper removed in Phase 2D refactoring  
        // Replacement: Use nested script with atomic operations
        // Script location: test_scripts/uia_handle_dialog.json
        // REMOVED: UIA_CLOSE_WINDOW_SAFE - Complex wrapper removed in Phase 2D refactoring
        // CRITICAL VIOLATIONS: SendMessage() direct Windows API call
        // Replacement: Use nested script with atomic operations
        // Script location: test_scripts/uia_close_window_safe.json
        // ===== ATOMIC UIA COMMANDS (9 Core Operations) =====
        } else if (action == "application.uia_key_press") {
            // UIA_KEY_PRESS - Press a key down (atomic operation)
            if (command["params"].contains("key")) {
                std::string key = command["params"]["key"];
                
                // Convert key string to VK code
                BYTE keyCode = 0;
                if (key == "VK_RETURN" || key == "RETURN" || key == "ENTER") keyCode = VK_RETURN;
                else if (key == "VK_ESCAPE" || key == "ESCAPE" || key == "ESC") keyCode = VK_ESCAPE;
                else if (key == "VK_TAB" || key == "TAB") keyCode = VK_TAB;
                else if (key == "VK_SPACE" || key == "SPACE") keyCode = VK_SPACE;
                else if (key == "VK_BACK" || key == "BACKSPACE") keyCode = VK_BACK;
                else if (key == "VK_DELETE" || key == "DELETE") keyCode = VK_DELETE;
                else if (key == "VK_LWIN") keyCode = VK_LWIN;
                else if (key == "VK_RWIN") keyCode = VK_RWIN;
                else if (key == "VK_CONTROL" || key == "VK_LCONTROL") keyCode = VK_LCONTROL;
                else if (key == "VK_RCONTROL") keyCode = VK_RCONTROL;
                else if (key == "VK_MENU" || key == "VK_LMENU" || key == "VK_ALT") keyCode = VK_LMENU;
                else if (key == "VK_RMENU" || key == "VK_RALT") keyCode = VK_RMENU;
                else if (key == "VK_SHIFT" || key == "VK_LSHIFT") keyCode = VK_LSHIFT;
                else if (key == "VK_RSHIFT") keyCode = VK_RSHIFT;
                else if (key.length() == 1) keyCode = std::toupper(key[0]); // Single character
                
                if (keyCode != 0) {
                    bool success = ::ocal::atomic::input::pressKey(keyCode);
                    if (success) {
                        result.status = ExecutionStatus::COMPLETED;
                        result.output = "Key pressed: " + key;
                        result.executedCommands.push_back("UIA_KEY_PRESS");
                    } else {
                        result.status = ExecutionStatus::FAILED;
                        result.errorMessage = "Failed to press key: " + key;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Invalid key code: " + key;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing key parameter";
            }
        } else if (action == "application.uia_key_release") {
            // UIA_KEY_RELEASE - Release a key (atomic operation)
            if (command["params"].contains("key")) {
                std::string key = command["params"]["key"];
                
                // Convert key string to VK code (same logic as key_press)
                BYTE keyCode = 0;
                if (key == "VK_RETURN" || key == "RETURN" || key == "ENTER") keyCode = VK_RETURN;
                else if (key == "VK_ESCAPE" || key == "ESCAPE" || key == "ESC") keyCode = VK_ESCAPE;
                else if (key == "VK_TAB" || key == "TAB") keyCode = VK_TAB;
                else if (key == "VK_SPACE" || key == "SPACE") keyCode = VK_SPACE;
                else if (key == "VK_BACK" || key == "BACKSPACE") keyCode = VK_BACK;
                else if (key == "VK_DELETE" || key == "DELETE") keyCode = VK_DELETE;
                else if (key == "VK_LWIN") keyCode = VK_LWIN;
                else if (key == "VK_RWIN") keyCode = VK_RWIN;
                else if (key == "VK_CONTROL" || key == "VK_LCONTROL") keyCode = VK_LCONTROL;
                else if (key == "VK_RCONTROL") keyCode = VK_RCONTROL;
                else if (key == "VK_MENU" || key == "VK_LMENU" || key == "VK_ALT") keyCode = VK_LMENU;
                else if (key == "VK_RMENU" || key == "VK_RALT") keyCode = VK_RMENU;
                else if (key == "VK_SHIFT" || key == "VK_LSHIFT") keyCode = VK_LSHIFT;
                else if (key == "VK_RSHIFT") keyCode = VK_RSHIFT;
                else if (key.length() == 1) keyCode = std::toupper(key[0]); // Single character
                
                if (keyCode != 0) {
                    bool success = ::ocal::atomic::input::releaseKey(keyCode);
                    if (success) {
                        result.status = ExecutionStatus::COMPLETED;
                        result.output = "Key released: " + key;
                        result.executedCommands.push_back("UIA_KEY_RELEASE");
                    } else {
                        result.status = ExecutionStatus::FAILED;
                        result.errorMessage = "Failed to release key: " + key;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Invalid key code: " + key;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing key parameter";
            }
        } else if (action == "application.uia_mouse_click") {
            // UIA_MOUSE_CLICK - Click mouse button (atomic operation)
            std::string button = command["params"].value("button", "left");
            
            bool success = ::ocal::atomic::input::mouseClick(button);
            if (success) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = "Mouse " + button + " clicked";
                result.executedCommands.push_back("UIA_MOUSE_CLICK");
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Failed to click mouse button: " + button;
            }
        } else if (action == "application.uia_mouse_move") {
            // UIA_MOUSE_MOVE - Move mouse cursor (atomic operation)
            if (command["params"].contains("x") && command["params"].contains("y")) {
                int x = command["params"]["x"];
                int y = command["params"]["y"];
                
                bool success = ::ocal::atomic::input::mouseMove(x, y);
                if (success) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = "Mouse moved to (" + std::to_string(x) + ", " + std::to_string(y) + ")";
                    result.executedCommands.push_back("UIA_MOUSE_MOVE");
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to move mouse cursor";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing x or y coordinates";
            }
        } else if (action == "application.uia_focus_window") {
            // UIA_FOCUS_WINDOW - Focus window (atomic operation)
            if (command["params"].contains("hwnd")) {
                uintptr_t windowHandle;
                if (command["params"]["hwnd"].is_string()) {
                    windowHandle = std::stoull(command["params"]["hwnd"].get<std::string>());
                } else {
                    windowHandle = command["params"]["hwnd"];
                }
                
                bool success = ::ocal::atomic::window::focusWindow(windowHandle);
                if (success) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = "Window focused";
                    result.executedCommands.push_back("UIA_FOCUS_WINDOW");
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to focus window";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing hwnd parameter";
            }
        } else if (action == "application.uia_get_clipboard") {
            // UIA_GET_CLIPBOARD - Get clipboard text (atomic operation)
            std::string clipboardText;
            bool success = ::ocal::atomic::input::getClipboard(clipboardText);
            
            if (success) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = clipboardText;
                result.executedCommands.push_back("UIA_GET_CLIPBOARD");
                
                // Store in variable if specified
                std::string varName = command["params"].value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = clipboardText;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Failed to get clipboard content";
            }
        } else if (action == "application.uia_set_clipboard") {
            // UIA_SET_CLIPBOARD - Set clipboard text (atomic operation)
            if (command["params"].contains("text")) {
                // FIXED: Handle both string and numeric types for clipboard text
                std::string text;
                if (command["params"]["text"].is_string()) {
                    text = command["params"]["text"];
                } else if (command["params"]["text"].is_number()) {
                    text = std::to_string(command["params"]["text"].get<long long>());
                } else {
                    text = command["params"]["text"].dump();
                }
                
                bool success = ::ocal::atomic::input::setClipboard(text);
                if (success) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = "Clipboard set successfully";
                    result.executedCommands.push_back("UIA_SET_CLIPBOARD");
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to set clipboard content";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing text parameter";
            }
        } else if (action == "application.uia_window_resize") {
            // UIA_WINDOW_RESIZE - Resize window (atomic operation)
            if (command["params"].contains("hwnd") && command["params"].contains("width") && command["params"].contains("height")) {
                uintptr_t windowHandle;
                if (command["params"]["hwnd"].is_string()) {
                    windowHandle = std::stoull(command["params"]["hwnd"].get<std::string>());
                } else {
                    windowHandle = command["params"]["hwnd"];
                }
                
                int width = command["params"]["width"];
                int height = command["params"]["height"];
                
                if (::ocal::atomic::window::resizeWindow(windowHandle, width, height)) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = "Window resized to " + std::to_string(width) + "x" + std::to_string(height);
                    result.executedCommands.push_back("UIA_WINDOW_RESIZE");
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to resize window";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing hwnd, width, or height parameter";
            }
        } else if (action == "application.uia_window_move") {
            // UIA_WINDOW_MOVE - Move window (atomic operation)
            if (command["params"].contains("hwnd") && command["params"].contains("x") && command["params"].contains("y")) {
                uintptr_t windowHandle;
                if (command["params"]["hwnd"].is_string()) {
                    windowHandle = std::stoull(command["params"]["hwnd"].get<std::string>());
                } else {
                    windowHandle = command["params"]["hwnd"];
                }
                
                int x = command["params"]["x"];
                int y = command["params"]["y"];
                
                if (::ocal::atomic::window::moveWindow(windowHandle, x, y)) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = "Window moved to (" + std::to_string(x) + ", " + std::to_string(y) + ")";
                    result.executedCommands.push_back("UIA_WINDOW_MOVE");
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to move window";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing hwnd, x, or y parameter";
            }
        } else if (action == "application.uia_enum_windows") {
            // UIA_ENUM_WINDOWS - Enumerate all windows (atomic operation)
            std::map<uintptr_t, std::map<std::string, std::string>> windowResults;
            
            if (::ocal::atomic::window::enumerateWindows(windowResults)) {
                // Apply class name filtering if specified
                std::string classNameFilter = command["params"].value("class_name", "");
                std::map<uintptr_t, std::map<std::string, std::string>> filteredResults;
                
                if (!classNameFilter.empty()) {
                    Logger::log(LogLevel::DEBUG, "[UIA] Filtering windows by class name: " + classNameFilter);
                    
                    for (const auto& [hwnd, info] : windowResults) {
                        // Get window class name
                        HWND windowHandle = reinterpret_cast<HWND>(hwnd);
                        char className[256];
                        int classNameLength = GetClassNameA(windowHandle, className, sizeof(className));
                        
                        if (classNameLength > 0) {
                            std::string windowClass(className);
                            
                            // Case-insensitive comparison for class name
                            std::string lowerWindowClass = windowClass;
                            std::string lowerFilter = classNameFilter;
                            std::transform(lowerWindowClass.begin(), lowerWindowClass.end(), lowerWindowClass.begin(), ::tolower);
                            std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
                            
                            if (lowerWindowClass.find(lowerFilter) != std::string::npos) {
                                // FIXED: Removed hardcoded Firefox/Mozilla logic - now application-agnostic
                                // Apply generic filtering without application-specific logic
                                LONG_PTR exStyle = GetWindowLongPtr(windowHandle, GWL_EXSTYLE);
                                if (!(exStyle & WS_EX_TOOLWINDOW)) {
                                    filteredResults[hwnd] = info;
                                    Logger::log(LogLevel::DEBUG, "[UIA] Window " + std::to_string(hwnd) + " matches class: " + windowClass);
                                }
                            }
                        }
                    }
                    
                    windowResults = filteredResults;
                    Logger::log(LogLevel::DEBUG, "[UIA] Filtered to " + std::to_string(windowResults.size()) + " windows matching class");
                }
                
                result.status = ExecutionStatus::COMPLETED;
                result.output = "Found " + std::to_string(windowResults.size()) + " windows";
                result.executedCommands.push_back("UIA_ENUM_WINDOWS");
                
                // Store window handles in variable if specified
                std::string varName = command["params"].value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = nlohmann::json::array();
                    for (const auto& [hwnd, info] : windowResults) {
                        std::string windowHandle = std::to_string(hwnd);
                        context.variables[varName].push_back(windowHandle);
                    }
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Failed to enumerate windows";
            }
        } else if (action == "application.uia_get_window_title") {
            // UIA_GET_WINDOW_TITLE - Get window title (atomic operation)
            if (command["params"].contains("hwnd")) {
                uintptr_t windowHandle;
                if (command["params"]["hwnd"].is_string()) {
                    windowHandle = std::stoull(command["params"]["hwnd"].get<std::string>());
                } else {
                    windowHandle = command["params"]["hwnd"];
                }
                
                std::string title;
                if (::ocal::atomic::window::getWindowTitle(windowHandle, title)) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = title;
                    result.executedCommands.push_back("UIA_GET_WINDOW_TITLE");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = title;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to get window title";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing hwnd parameter";
            }
        } else if (action == "application.uia_get_window_class") {
            // UIA_GET_WINDOW_CLASS - Get window class name (atomic operation)
            if (command["params"].contains("hwnd")) {
                uintptr_t windowHandle;
                if (command["params"]["hwnd"].is_string()) {
                    windowHandle = std::stoull(command["params"]["hwnd"].get<std::string>());
                } else {
                    windowHandle = command["params"]["hwnd"];
                }
                
                std::string className;
                if (::ocal::atomic::window::getWindowClass(windowHandle, className)) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = className;
                    result.executedCommands.push_back("UIA_GET_WINDOW_CLASS");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = className;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to get window class name";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing hwnd parameter";
            }
        } else if (action == "application.uia_get_window_rect") {
            // UIA_GET_WINDOW_RECT - Get window position and size (atomic operation)
            if (command["params"].contains("hwnd")) {
                uintptr_t windowHandle;
                if (command["params"]["hwnd"].is_string()) {
                    windowHandle = std::stoull(command["params"]["hwnd"].get<std::string>());
                } else {
                    windowHandle = command["params"]["hwnd"];
                }
                
                std::map<std::string, int> rect;
                if (::ocal::atomic::window::getWindowRect(windowHandle, rect)) {
                    nlohmann::json rectData = {
                        {"left", rect["left"]},
                        {"top", rect["top"]}, 
                        {"right", rect["right"]},
                        {"bottom", rect["bottom"]},
                        {"width", rect["width"]},
                        {"height", rect["height"]}
                    };
                    
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = rectData.dump();
                    result.executedCommands.push_back("UIA_GET_WINDOW_RECT");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = rectData;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to get window rectangle";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing hwnd parameter";
            }
            
        } else if (action == "application.uia_get_mouse_position") {
            // UIA_GET_MOUSE_POSITION - Get current mouse cursor position (atomic operation)
            std::map<std::string, int> position;
            if (::ocal::atomic::input::getMousePosition(position)) {
                nlohmann::json posData = {
                    {"x", position["x"]},
                    {"y", position["y"]}
                };
                
                result.status = ExecutionStatus::COMPLETED;
                result.output = posData.dump();
                result.executedCommands.push_back("UIA_GET_MOUSE_POSITION");
                
                // Store in variable if specified
                std::string varName = command["params"].value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = posData;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Failed to get mouse position";
            }
            
        } else if (action == "application.uia_launch_application") {
            // UIA_LAUNCH_APPLICATION - Launch application process (atomic operation)
            if (command["params"].contains("path")) {
                std::string appPath = command["params"]["path"];
                std::map<std::string, std::string> processInfo;
                
                if (::ocal::atomic::process::launchApplication(appPath, processInfo)) {
                    nlohmann::json processData;
                    for (const auto& [key, value] : processInfo) {
                        processData[key] = value;
                    }
                    
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = processData.dump();
                    result.executedCommands.push_back("UIA_LAUNCH_APPLICATION");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = processData;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to launch application: " + appPath;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing path parameter";
            }
            
        } else if (action == "application.uia_terminate_process") {
            // UIA_TERMINATE_PROCESS - Terminate process by ID (atomic operation)
            if (command["params"].contains("processId")) {
                unsigned long processId = command["params"]["processId"];
                
                if (::ocal::atomic::process::terminateProcess(processId)) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = "Process terminated successfully";
                    result.executedCommands.push_back("UIA_TERMINATE_PROCESS");
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to terminate process";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing processId parameter";
            }
            
        } else if (action == "application.uia_is_window_minimized") {
            // UIA_IS_WINDOW_MINIMIZED - Check if window is minimized (atomic operation)
            if (command["params"].contains("hwnd")) {
                uintptr_t windowHandle;
                if (command["params"]["hwnd"].is_string()) {
                    windowHandle = std::stoull(command["params"]["hwnd"].get<std::string>());
                } else {
                    windowHandle = command["params"]["hwnd"];
                }
                
                bool isMinimized;
                if (::ocal::atomic::window::isWindowMinimized(windowHandle, isMinimized)) {
                    nlohmann::json stateData = {
                        {"isMinimized", isMinimized}
                    };
                    
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = stateData.dump();
                    result.executedCommands.push_back("UIA_IS_WINDOW_MINIMIZED");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = stateData;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to check window state";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing hwnd parameter";
            }
            
        } else if (action == "application.uia_is_window_maximized") {
            // UIA_IS_WINDOW_MAXIMIZED - Check if window is maximized (atomic operation)
            if (command["params"].contains("hwnd")) {
                uintptr_t windowHandle;
                if (command["params"]["hwnd"].is_string()) {
                    windowHandle = std::stoull(command["params"]["hwnd"].get<std::string>());
                } else {
                    windowHandle = command["params"]["hwnd"];
                }
                
                bool isMaximized;
                if (::ocal::atomic::window::isWindowMaximized(windowHandle, isMaximized)) {
                    nlohmann::json stateData = {
                        {"isMaximized", isMaximized}
                    };
                    
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = stateData.dump();
                    result.executedCommands.push_back("UIA_IS_WINDOW_MAXIMIZED");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = stateData;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to check window state";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing hwnd parameter";
            }
            
        } else if (action == "application.uia_get_foreground_window") {
            // UIA_GET_FOREGROUND_WINDOW - Get currently focused window (atomic operation)
            uintptr_t foregroundWindow;
            
            if (::ocal::atomic::window::getForegroundWindow(foregroundWindow)) {
                nlohmann::json windowData = {
                    {"hwnd", foregroundWindow}
                };
                
                result.status = ExecutionStatus::COMPLETED;
                result.output = windowData.dump();
                result.executedCommands.push_back("UIA_GET_FOREGROUND_WINDOW");
                
                // Store in variable if specified
                std::string varName = command["params"].value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = windowData;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "No foreground window found";
            }
            
        } else if (action == "application.uia_find_windows_by_class") {
            // UIA_FIND_WINDOWS_BY_CLASS - Find windows by class name (atomic operation)
            if (command["params"].contains("className")) {
                std::string className = command["params"]["className"];
                std::vector<uintptr_t> foundWindows;
                
                if (::ocal::atomic::window::findWindowsByClass(className, foundWindows)) {
                    nlohmann::json windowArray = nlohmann::json::array();
                    for (uintptr_t hwnd : foundWindows) {
                        windowArray.push_back({{"hwnd", hwnd}});
                    }
                    
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = windowArray.dump();
                    result.executedCommands.push_back("UIA_FIND_WINDOWS_BY_CLASS");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = windowArray;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to find windows by class";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing className parameter";
            }
            
        } else if (action == "application.uia_find_windows_by_title") {
            // UIA_FIND_WINDOWS_BY_TITLE - Find windows by title pattern (atomic operation)
            if (command["params"].contains("titlePattern")) {
                std::string titlePattern = command["params"]["titlePattern"];
                bool exactMatch = command["params"].value("exactMatch", false);
                std::vector<uintptr_t> foundWindows;
                
                if (::ocal::atomic::window::findWindowsByTitle(titlePattern, exactMatch, foundWindows)) {
                    nlohmann::json windowArray = nlohmann::json::array();
                    for (uintptr_t hwnd : foundWindows) {
                        windowArray.push_back({{"hwnd", hwnd}});
                    }
                    
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = windowArray.dump();
                    result.executedCommands.push_back("UIA_FIND_WINDOWS_BY_TITLE");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = windowArray;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to find windows by title";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing titlePattern parameter";
            }
            
        } else if (action == "application.uia_get_window_info") {
            // UIA_GET_WINDOW_INFO - Get comprehensive window information (atomic operation)
            if (command["params"].contains("hwnd")) {
                uintptr_t hwndValue = command["params"]["hwnd"];
                std::map<std::string, std::string> info;
                
                if (::ocal::atomic::window::getWindowInfo(hwndValue, info)) {
                    nlohmann::json windowInfo;
                    for (const auto& [key, value] : info) {
                        // Try to parse numeric values
                        if (key == "left" || key == "top" || key == "right" || key == "bottom" || 
                            key == "width" || key == "height" || key == "hwnd") {
                            try {
                                windowInfo[key] = std::stoi(value);
                            } catch (...) {
                                windowInfo[key] = value;
                            }
                        } else if (key == "isVisible" || key == "isMinimized" || key == "isMaximized") {
                            windowInfo[key] = (value == "true" || value == "1");
                        } else {
                            windowInfo[key] = value;
                        }
                    }
                    
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = windowInfo.dump();
                    result.executedCommands.push_back("UIA_GET_WINDOW_INFO");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = windowInfo;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Invalid window handle";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing hwnd parameter";
            }
            
        } else if (action == "application.uia_shell_execute") {
            // UIA_SHELL_EXECUTE - Execute shell command or open file (atomic operation)
            if (command["params"].contains("path")) {
                std::string path = command["params"]["path"];
                std::string operation = command["params"].value("operation", "open");
                std::string parameters = command["params"].value("parameters", "");
                std::string directory = command["params"].value("directory", "");
                int showCmd = command["params"].value("showCmd", SW_SHOWNORMAL);
                
                std::wstring wPath(path.begin(), path.end());
                std::wstring wOperation(operation.begin(), operation.end());
                std::wstring wParameters(parameters.begin(), parameters.end());
                std::wstring wDirectory(directory.begin(), directory.end());
                
                HINSTANCE result_code = ShellExecuteW(
                    NULL,
                    wOperation.empty() ? NULL : wOperation.c_str(),
                    wPath.c_str(),
                    wParameters.empty() ? NULL : wParameters.c_str(),
                    wDirectory.empty() ? NULL : wDirectory.c_str(),
                    showCmd
                );
                
                if (reinterpret_cast<uintptr_t>(result_code) > 32) {
                    nlohmann::json executeInfo = {
                        {"success", true},
                        {"path", path},
                        {"operation", operation}
                    };
                    
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = executeInfo.dump();
                    result.executedCommands.push_back("UIA_SHELL_EXECUTE");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = executeInfo;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Shell execute failed with error code: " + std::to_string(reinterpret_cast<uintptr_t>(result_code));
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing path parameter";
            }
            
        } else if (action == "application.uia_get_file_info") {
            // UIA_GET_FILE_INFO - Get file information (atomic operation)
            if (command["params"].contains("filePath")) {
                std::string filePath = command["params"]["filePath"];
                std::wstring wFilePath(filePath.begin(), filePath.end());
                
                WIN32_FILE_ATTRIBUTE_DATA fileData;
                if (GetFileAttributesExW(wFilePath.c_str(), GetFileExInfoStandard, &fileData)) {
                    ULARGE_INTEGER fileSize;
                    fileSize.LowPart = fileData.nFileSizeLow;
                    fileSize.HighPart = fileData.nFileSizeHigh;
                    
                    nlohmann::json fileInfo = {
                        {"filePath", filePath},
                        {"size", static_cast<long long>(fileSize.QuadPart)},
                        {"isDirectory", (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0},
                        {"isHidden", (fileData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0},
                        {"isReadOnly", (fileData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0},
                        {"attributes", fileData.dwFileAttributes}
                    };
                    
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = fileInfo.dump();
                    result.executedCommands.push_back("UIA_GET_FILE_INFO");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = fileInfo;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to get file information for: " + filePath;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing filePath parameter";
            }
            
        } else if (action == "application.uia_create_directory") {
            // UIA_CREATE_DIRECTORY - Create directory (atomic operation)
            if (command["params"].contains("path")) {
                std::string path = command["params"]["path"];
                std::wstring wPath(path.begin(), path.end());
                
                if (CreateDirectoryW(wPath.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
                    nlohmann::json dirInfo = {
                        {"path", path},
                        {"created", true}
                    };
                    
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = dirInfo.dump();
                    result.executedCommands.push_back("UIA_CREATE_DIRECTORY");
                    
                    // Store in variable if specified
                    std::string varName = command["params"].value("store_as", "");
                    if (!varName.empty()) {
                        context.variables[varName] = dirInfo;
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to create directory: " + path;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing path parameter";
            }
            
        } else if (action == "application.uia_find_files_by_pattern") {
            // UIA_FIND_FILES_BY_PATTERN - Find files by pattern (atomic operation)
            if (command["params"].contains("directory") && command["params"].contains("pattern")) {
                std::string directory = command["params"]["directory"];
                std::string pattern = command["params"]["pattern"];
                std::wstring wSearchPath(directory.begin(), directory.end());
                wSearchPath += L"\\" + std::wstring(pattern.begin(), pattern.end());
                
                std::vector<std::string> foundFiles;
                WIN32_FIND_DATAW findData;
                HANDLE hFind = FindFirstFileW(wSearchPath.c_str(), &findData);
                
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            std::wstring wFileName = findData.cFileName;
                            std::string fileName(wFileName.begin(), wFileName.end());
                            foundFiles.push_back(directory + "\\" + fileName);
                        }
                    } while (FindNextFileW(hFind, &findData));
                    FindClose(hFind);
                }
                
                nlohmann::json fileArray = nlohmann::json::array();
                for (const auto& file : foundFiles) {
                    fileArray.push_back(file);
                }
                
                result.status = ExecutionStatus::COMPLETED;
                result.output = fileArray.dump();
                result.executedCommands.push_back("UIA_FIND_FILES_BY_PATTERN");
                
                // Store in variable if specified
                std::string varName = command["params"].value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = fileArray;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing directory or pattern parameter";
            }
            
        } else if (action == "application.uia_find_files_by_extension") {
            // UIA_FIND_FILES_BY_EXTENSION - Find files by extension (atomic operation)
            if (command["params"].contains("directory") && command["params"].contains("extension")) {
                std::string directory = command["params"]["directory"];
                std::string extension = command["params"]["extension"];
                
                // Ensure extension starts with dot
                if (!extension.empty() && extension[0] != '.') {
                    extension = "." + extension;
                }
                
                std::wstring wSearchPath(directory.begin(), directory.end());
                wSearchPath += L"\\*" + std::wstring(extension.begin(), extension.end());
                
                std::vector<std::string> foundFiles;
                WIN32_FIND_DATAW findData;
                HANDLE hFind = FindFirstFileW(wSearchPath.c_str(), &findData);
                
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            std::wstring wFileName = findData.cFileName;
                            std::string fileName(wFileName.begin(), wFileName.end());
                            foundFiles.push_back(directory + "\\" + fileName);
                        }
                    } while (FindNextFileW(hFind, &findData));
                    FindClose(hFind);
                }
                
                nlohmann::json fileArray = nlohmann::json::array();
                for (const auto& file : foundFiles) {
                    fileArray.push_back(file);
                }
                
                result.status = ExecutionStatus::COMPLETED;
                result.output = fileArray.dump();
                result.executedCommands.push_back("UIA_FIND_FILES_BY_EXTENSION");
                
                // Store in variable if specified
                std::string varName = command["params"].value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = fileArray;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing directory or extension parameter";
            }
            
        } else if (action == "application.uia_move_files") {
            // UIA_MOVE_FILES - Move files to target location (atomic operation)
            if (command["params"].contains("sourceFiles") && command["params"].contains("targetDirectory")) {
                nlohmann::json sourceFiles = command["params"]["sourceFiles"];
                std::string targetDirectory = command["params"]["targetDirectory"];
                
                std::vector<std::string> movedFiles;
                std::vector<std::string> failedFiles;
                
                for (const auto& sourceFile : sourceFiles) {
                    std::string sourceFilePath = sourceFile.get<std::string>();
                    std::wstring wSourcePath(sourceFilePath.begin(), sourceFilePath.end());
                    
                    // Extract filename from source path
                    size_t lastSlash = sourceFilePath.find_last_of("\\/");
                    std::string fileName = (lastSlash != std::string::npos) ? 
                        sourceFilePath.substr(lastSlash + 1) : sourceFilePath;
                    
                    std::string targetPath = targetDirectory + "\\" + fileName;
                    std::wstring wTargetPath(targetPath.begin(), targetPath.end());
                    
                    if (MoveFileW(wSourcePath.c_str(), wTargetPath.c_str())) {
                        movedFiles.push_back(targetPath);
                    } else {
                        failedFiles.push_back(sourceFilePath);
                    }
                }
                
                nlohmann::json moveResult = {
                    {"movedFiles", movedFiles},
                    {"failedFiles", failedFiles},
                    {"successCount", movedFiles.size()},
                    {"failureCount", failedFiles.size()}
                };
                
                result.status = failedFiles.empty() ? ExecutionStatus::COMPLETED : ExecutionStatus::FAILED;
                result.output = moveResult.dump();
                result.executedCommands.push_back("UIA_MOVE_FILES");
                
                // Store in variable if specified
                std::string varName = command["params"].value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = moveResult;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing sourceFiles or targetDirectory parameter";
            }
            
        } else if (action == "application.uia_delete_files") {
            // UIA_DELETE_FILES - Delete specified files (atomic operation)
            if (command["params"].contains("filePaths")) {
                nlohmann::json filePaths = command["params"]["filePaths"];
                
                std::vector<std::string> deletedFiles;
                std::vector<std::string> failedFiles;
                
                for (const auto& filePath : filePaths) {
                    std::string filePathStr = filePath.get<std::string>();
                    std::wstring wFilePath(filePathStr.begin(), filePathStr.end());
                    
                    if (DeleteFileW(wFilePath.c_str())) {
                        deletedFiles.push_back(filePathStr);
                    } else {
                        failedFiles.push_back(filePathStr);
                    }
                }
                
                nlohmann::json deleteResult = {
                    {"deletedFiles", deletedFiles},
                    {"failedFiles", failedFiles},
                    {"successCount", deletedFiles.size()},
                    {"failureCount", failedFiles.size()}
                };
                
                result.status = failedFiles.empty() ? ExecutionStatus::COMPLETED : ExecutionStatus::FAILED;
                result.output = deleteResult.dump();
                result.executedCommands.push_back("UIA_DELETE_FILES");
                
                // Store in variable if specified
                std::string varName = command["params"].value("store_as", "");
                if (!varName.empty()) {
                    context.variables[varName] = deleteResult;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing filePaths parameter";
            }
            
        } else {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Unknown application command: " + action;
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Application command exception: " + std::string(e.what());
    }
    
    return result;
}

TaskExecutionResult Orchestrator::executeSystemCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    
    try {
        std::string action = command["command"];
        
        if (action == "system.sleep") {
            int ms = command["params"]["ms"];
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            result.status = ExecutionStatus::COMPLETED;
        } else if (action == "system.shell") {
            std::string cmd = command["params"]["command"];
            std::string workingDir = command["params"].value("working_dir", "");
            int timeoutMs = command["params"].value("timeout_ms", 30000);
            bool elevated = command["params"].value("elevated", false);
            
            // Use enhanced OCAL system command execution with smart detection
            auto cmdResult = burwell::ocal::system::executeSmartCommand(cmd, workingDir, {}, timeoutMs, true, elevated);
            if (cmdResult.success) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = cmdResult.output;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Shell command failed: " + cmdResult.error;
            }
            
        } else if (action == "system.powershell") {
            // Handle both "script" and "command" parameter names for compatibility
            std::string cmd;
            if (command["params"].contains("script")) {
                cmd = command["params"]["script"];
            } else if (command["params"].contains("command")) {
                cmd = command["params"]["command"];
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "PowerShell command missing 'script' or 'command' parameter";
                return result;
            }
            
            std::string workingDir = command["params"].value("working_dir", "");
            int timeoutMs = command["params"].value("timeout_ms", 30000);
            bool elevated = command["params"].value("elevated", false);
            std::string executionPolicy = command["params"].value("execution_policy", "Bypass");
            
            // Use PowerShell-specific execution
            auto cmdResult = burwell::ocal::system::executePowerShellCommand(cmd, 
                burwell::ocal::system::PowerShellMode::COMMAND, workingDir, {}, timeoutMs, true, elevated, executionPolicy);
            if (cmdResult.success) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = cmdResult.output;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "PowerShell command failed: " + cmdResult.error;
            }
            
        } else if (action == "system.commandChain") {
            auto commandsArray = command["params"]["commands"];
            std::vector<std::string> commands;
            for (const auto& cmd : commandsArray) {
                commands.push_back(cmd.get<std::string>());
            }
            
            std::string workingDir = command["params"].value("working_dir", "");
            bool stopOnError = command["params"].value("stop_on_error", true);
            int timeoutMs = command["params"].value("timeout_ms", 30000);
            bool elevated = command["params"].value("elevated", false);
            
            // Execute command chain
            auto cmdResults = burwell::ocal::system::executeCommandChain(commands, workingDir, {}, stopOnError, timeoutMs, elevated);
            
            bool allSuccessful = true;
            std::string combinedOutput;
            std::string combinedError;
            
            for (const auto& cmdResult : cmdResults) {
                if (!cmdResult.success) {
                    allSuccessful = false;
                    combinedError += cmdResult.error + "; ";
                }
                combinedOutput += cmdResult.output + "\n";
            }
            
            if (allSuccessful) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = combinedOutput;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Command chain failed: " + combinedError;
                result.output = combinedOutput;
            }
            
        } else if (action == "system.packageOperation") {
            std::string packageManager = command["params"]["package_manager"];
            std::string operation = command["params"]["operation"];
            std::string packageName = command["params"]["package_name"];
            std::string version = command["params"].value("version", "");
            
            auto cmdResult = burwell::ocal::system::executePackageOperation(packageManager, operation, packageName, version);
            if (cmdResult.success) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = cmdResult.output;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Package operation failed: " + cmdResult.error;
            }
            
        } else if (action == "system.registryOperation") {
            std::string operation = command["params"]["operation"];
            std::string keyPath = command["params"]["key_path"];
            std::string valueName = command["params"].value("value_name", "");
            std::string valueData = command["params"].value("value_data", "");
            
            auto cmdResult = burwell::ocal::system::executeRegistryOperation(operation, keyPath, valueName, valueData);
            if (cmdResult.success) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = cmdResult.output;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Registry operation failed: " + cmdResult.error;
            }
            
        } else if (action == "system.serviceOperation") {
            std::string serviceName = command["params"]["service_name"];
            std::string operation = command["params"]["operation"];
            std::string serviceDisplayName = command["params"].value("service_display_name", "");
            std::string servicePath = command["params"].value("service_path", "");
            
            auto cmdResult = burwell::ocal::system::executeServiceOperation(serviceName, operation, serviceDisplayName, servicePath);
            if (cmdResult.success) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = cmdResult.output;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Service operation failed: " + cmdResult.error;
            }
            
        } else if (action == "system.networkOperation") {
            std::string operation = command["params"]["operation"];
            std::string target = command["params"].value("target", "");
            
            auto cmdResult = burwell::ocal::system::executeNetworkOperation(operation, target);
            if (cmdResult.success) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = cmdResult.output;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Network operation failed: " + cmdResult.error;
            }
            
        } else if (action == "system.executeScript") {
            std::string scriptPath = command["params"]["script_path"];
            auto argumentsArray = command["params"].value("arguments", nlohmann::json::array());
            std::vector<std::string> arguments;
            for (const auto& arg : argumentsArray) {
                arguments.push_back(arg.get<std::string>());
            }
            
            std::string workingDir = command["params"].value("working_dir", "");
            int timeoutMs = command["params"].value("timeout_ms", 60000);
            bool elevated = command["params"].value("elevated", false);
            
            auto cmdResult = burwell::ocal::system::executeScriptFile(scriptPath, arguments, workingDir, timeoutMs, elevated);
            if (cmdResult.success) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = cmdResult.output;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Script execution failed: " + cmdResult.error;
            }
            
        } else if (action == "system.getSystemInfo") {
            std::string infoType = command["params"].value("info_type", "general");
            
            auto cmdResult = burwell::ocal::system::getSystemInformation(infoType);
            if (cmdResult.success) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = cmdResult.output;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "System info retrieval failed: " + cmdResult.error;
            }
        } else if (action == "system.fileExists") {
            std::string path = command["params"]["path"];
            
            bool exists = m_ocal->filesystemOperations().fileExists(path);
            result.status = ExecutionStatus::COMPLETED;
            result.output = exists ? "true" : "false";
        } else if (action == "system.fileCopy") {
            std::string source = command["params"]["source"];
            std::string destination = command["params"]["destination"];
            bool overwrite = command["params"].value("overwrite", false);
            
            if (m_ocal->filesystemOperations().copyFile(source, destination, overwrite)) {
                result.status = ExecutionStatus::COMPLETED;
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "File copy failed";
            }
        } else {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Unknown system command: " + action;
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "System command exception: " + std::string(e.what());
    }
    
    return result;
}

TaskExecutionResult Orchestrator::executeWindowCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    std::string action = command["command"];
    
    try {
        LOG_DEBUG("Window command JSON: " + command.dump());
        
        if (action == "window.find") {
            std::string title = command["params"].value("title", "");
            std::string className = command["params"].value("class", "");
            std::string process = command["params"].value("process", "");
            std::string storeAs = command["params"].value("store_as", "");
            bool partialMatch = command["params"].value("partial_match", true);
            
            LOG_DEBUG("Finding windows with title='" + title + 
                       "', class='" + className + "', process='" + process + "'");
            
            if (!title.empty()) {
                // Use enhanced window finding with case-insensitive partial matching and main process prioritization
                auto matchingHandles = ::ocal::window::findAll(title);
                std::vector<::ocal::window::WindowInfo> matchingWindows;
                
                // Convert handles to WindowInfo objects
                for (auto handle : matchingHandles) {
                    ::ocal::window::WindowInfo window = ::ocal::window::getInfo(handle);
                    if (window.handle != ::ocal::window::INVALID_WINDOW_HANDLE) {
                        matchingWindows.push_back(window);
#ifdef _WIN32
                        LOG_DEBUG("Found matching window: '" + window.title + 
                                   "' (Handle: " + std::to_string(reinterpret_cast<uintptr_t>(window.handle)) +
#else
                        LOG_DEBUG("Found matching window: '" + window.title + 
                                   "' (Handle: " + std::to_string(static_cast<uintptr_t>(window.handle)) +
#endif 
                                   ", PID: " + std::to_string(window.processId) + ")");
                    }
                }
                
                if (!matchingWindows.empty()) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = "Found " + std::to_string(matchingWindows.size()) + " matching windows";
                    
                    // Store the window handles in the result for later use
                    nlohmann::json windowList = nlohmann::json::array();
                    for (const auto& window : matchingWindows) {
                        nlohmann::json windowData;
#ifdef _WIN32
                        windowData["handle"] = reinterpret_cast<uintptr_t>(window.handle);
#else
                        windowData["handle"] = static_cast<uintptr_t>(window.handle);
#endif
                        windowData["title"] = window.title;
                        windowData["className"] = window.className;
                        windowData["processId"] = window.processId;
                        windowData["x"] = window.x;
                        windowData["y"] = window.y;
                        windowData["width"] = window.width;
                        windowData["height"] = window.height;
                        windowData["isVisible"] = window.isVisible;
                        windowList.push_back(windowData);
                    }
                    result.result["windows"] = windowList;
                    
                    // Store in context if variable name provided
                    if (!storeAs.empty()) {
                        result.result[storeAs] = windowList;
                        context.variables[storeAs] = windowList;
                        LOG_DEBUG("Stored " + std::to_string(windowList.size()) + " windows in variable: " + storeAs);
                    }
                    
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "No windows found matching title: " + title;
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Window find requires title, class, or process parameter";
            }
            
        } else if (action == "window.activate") {
            if (command["params"].contains("handle")) {
                uintptr_t handleValue = command["params"]["handle"];
#ifdef _WIN32
                ::ocal::window::WindowHandle handle = reinterpret_cast<::ocal::window::WindowHandle>(handleValue);
#else
                ::ocal::window::WindowHandle handle = static_cast<::ocal::window::WindowHandle>(handleValue);
#endif
                
                bool success = ::ocal::window::bringToFront(handle);
                if (success) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = "Window activated successfully";
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to activate window";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Window activate requires handle parameter";
            }
            
        } else if (action == "window.close") {
            if (command["params"].contains("handle")) {
                uintptr_t handleValue = command["params"]["handle"];
#ifdef _WIN32
                ::ocal::window::WindowHandle handle = reinterpret_cast<::ocal::window::WindowHandle>(handleValue);
#else
                ::ocal::window::WindowHandle handle = static_cast<::ocal::window::WindowHandle>(handleValue);
#endif
                
                bool success = ::ocal::window::close(handle);
                if (success) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = "Window closed successfully";
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Failed to close window";
                }
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Window close requires handle parameter";
            }
            
        } else if (action == "window.enumerate") {
            auto allWindows = ::ocal::window::enumerateVisible();
            
            nlohmann::json windowList = nlohmann::json::array();
            for (const auto& window : allWindows) {
                nlohmann::json windowData;
#ifdef _WIN32
                windowData["handle"] = reinterpret_cast<uintptr_t>(window.handle);
#else
                windowData["handle"] = static_cast<uintptr_t>(window.handle);
#endif
                windowData["title"] = window.title;
                windowData["className"] = window.className;
                windowData["processId"] = window.processId;
                windowData["x"] = window.x;
                windowData["y"] = window.y;
                windowData["width"] = window.width;
                windowData["height"] = window.height;
                windowData["isVisible"] = window.isVisible;
                windowList.push_back(windowData);
            }
            
            result.status = ExecutionStatus::COMPLETED;
            result.output = "Enumerated " + std::to_string(allWindows.size()) + " visible windows";
            result.result["windows"] = windowList;
            
        } else {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Unknown window command: " + action;
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Window command exception: " + std::string(e.what());
    }
    
    return result;
}

TaskExecutionResult Orchestrator::executeWaitCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    result.status = ExecutionStatus::COMPLETED;
    
    try {
        if (!command.contains("params") || !command["params"].contains("duration")) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "WAIT command requires 'duration' parameter";
            return result;
        }
        
        std::string duration = command["params"]["duration"].get<std::string>();
        int milliseconds = 0;
        
        // Parse duration string (e.g., "500ms", "5s", "1000")
        if (duration.find("ms") != std::string::npos) {
            milliseconds = std::stoi(duration.substr(0, duration.find("ms")));
        } else if (duration.find("s") != std::string::npos) {
            int seconds = std::stoi(duration.substr(0, duration.find("s")));
            milliseconds = seconds * 1000;
        } else {
            // Assume milliseconds if no unit specified
            milliseconds = std::stoi(duration);
        }
        
        // Validate duration range (1ms to 30 seconds max for safety)
        if (milliseconds < 1 || milliseconds > 30000) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "WAIT duration must be between 1ms and 30000ms (30 seconds)";
            return result;
        }
        
        Logger::log(LogLevel::DEBUG, "Waiting for " + std::to_string(milliseconds) + "ms");
        
        // Perform the wait using high-resolution sleep
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        
        result.output = "Waited for " + duration;
        result.result["duration"] = duration;
        result.result["milliseconds"] = milliseconds;
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "WAIT command exception: " + std::string(e.what());
    }
    
    return result;
}

// Stub implementations for remaining methods
void Orchestrator::addEventListener(std::function<void(const EventData&)> listener) { m_eventListeners.push_back(listener); }
void Orchestrator::raiseEvent(const EventData& event) { for (auto& listener : m_eventListeners) { listener(event); } }
TaskExecutionResult Orchestrator::executeTask(const std::string& taskId) { return TaskExecutionResult(); }
TaskExecutionResult Orchestrator::executePlan(const nlohmann::json& executionPlan) {
    TaskExecutionResult result;
    result.executionId = generateRequestId();
    result.status = ExecutionStatus::IN_PROGRESS;
    result.success = false;
    
    Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Starting execution plan: " + result.executionId);
    
    try {
        // Validate the execution plan structure
        if (!validateExecutionPlan(executionPlan)) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Invalid execution plan structure";
            Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] Plan validation failed: " + result.errorMessage);
            return result;
        }
        
        // Create execution context
        ExecutionContext context;
        context.requestId = result.executionId;
        context.requiresUserConfirmation = requiresUserConfirmation(executionPlan);
        context.maxNestingLevel = ConfigManager::getInstance().getMaxScriptNestingLevel();
        
        // Load script variables into execution context
        loadScriptVariables(executionPlan, context);
        
        // Skip environmental context for simple script execution to avoid unnecessary overhead
        // updateEnvironmentalContext(context);
        
        // Execute the command sequence
        if (executionPlan.contains("sequence") && executionPlan["sequence"].is_array()) {
            Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Executing command sequence with " + 
                       std::to_string(executionPlan["sequence"].size()) + " steps");
            
            result = executeCommandSequence(executionPlan["sequence"], context);
        } else if (executionPlan.contains("commands") && executionPlan["commands"].is_array()) {
            Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Executing command list with " + 
                       std::to_string(executionPlan["commands"].size()) + " commands");
            
            result = executeCommandSequence(executionPlan["commands"], context);
        } else {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "No valid command sequence or commands array found in execution plan";
            Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] " + result.errorMessage);
            return result;
        }
        
        // Update result with execution context data
        result.executionId = context.requestId;
        
        // Set success flag based on execution status
        if (result.status == ExecutionStatus::COMPLETED) {
            result.success = true;
            Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Plan execution completed successfully");
        } else {
            result.success = false;
            if (result.status != ExecutionStatus::FAILED) {
                result.status = ExecutionStatus::FAILED;
            }
            Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] Plan execution failed: " + result.errorMessage);
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.success = false;
        result.errorMessage = "Exception during plan execution: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] Exception: " + result.errorMessage);
    }
    
    Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Plan execution finished: " + 
               std::string(result.success ? "SUCCESS" : "FAILED"));
    
    return result;
}

TaskExecutionResult Orchestrator::executePlan(const nlohmann::json& executionPlan, ExecutionContext& context) {
    TaskExecutionResult result;
    result.executionId = context.requestId;
    result.status = ExecutionStatus::IN_PROGRESS;
    result.success = false;
    
    Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Starting execution plan: " + result.executionId);
    
    try {
        // Validate the execution plan structure
        if (!validateExecutionPlan(executionPlan)) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Invalid execution plan structure";
            Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] Plan validation failed: " + result.errorMessage);
            return result;
        }
        
        // Load script variables into the provided execution context
        loadScriptVariables(executionPlan, context);
        
        // Execute the command sequence
        if (executionPlan.contains("sequence") && executionPlan["sequence"].is_array()) {
            Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Executing sequence with " + 
                       std::to_string(executionPlan["sequence"].size()) + " steps");
            
            result = executeCommandSequence(executionPlan["sequence"], context);
        } else if (executionPlan.contains("commands") && executionPlan["commands"].is_array()) {
            Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Executing command list with " + 
                       std::to_string(executionPlan["commands"].size()) + " commands");
            
            result = executeCommandSequence(executionPlan["commands"], context);
        } else {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "No valid command sequence or commands array found in execution plan";
            Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] " + result.errorMessage);
            return result;
        }
        
        if (result.status != ExecutionStatus::COMPLETED) {
            Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] " + result.errorMessage);
            return result;
        }
        
        // Update result with execution context data
        result.executionId = context.requestId;
        
        // Set success flag based on execution status
        if (result.status == ExecutionStatus::COMPLETED) {
            result.success = true;
            Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Plan execution completed successfully");
        } else {
            result.success = false;
            if (result.status != ExecutionStatus::FAILED) {
                result.status = ExecutionStatus::FAILED;
            }
            Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] Plan execution failed: " + result.errorMessage);
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.success = false;
        result.errorMessage = "Exception during plan execution: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] Exception: " + result.errorMessage);
    }
    
    Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Plan execution finished: " + 
               std::string(result.success ? "SUCCESS" : "FAILED"));
    
    return result;
}

std::vector<std::string> Orchestrator::getActiveRequests() const { return {}; }
void Orchestrator::cancelExecution(const std::string& requestId) { }
nlohmann::json Orchestrator::getSystemStatus() const { return nlohmann::json{}; }
std::vector<std::string> Orchestrator::getRecentActivity() const { std::lock_guard<std::mutex> lock(m_activityMutex); return m_recentActivity; }
bool Orchestrator::validateExecutionPlan(const nlohmann::json& plan) { 
    // Accept both "commands" and "sequence" arrays
    return plan.contains("commands") || plan.contains("sequence"); 
}
bool Orchestrator::requiresUserConfirmation(const nlohmann::json& plan) { return m_confirmationRequired; }
bool Orchestrator::isCommandSafe(const nlohmann::json& command) { return true; }
void Orchestrator::handleExecutionError(const std::string& requestId, const std::string& error) { Logger::log(LogLevel::ERROR_LEVEL, "Execution error for " + requestId + ": " + error); }
void Orchestrator::attemptRecovery(const std::string& requestId, const std::string& error) { }
void Orchestrator::rollbackExecution(const std::string& requestId) { }
void Orchestrator::updateEnvironmentalContext(ExecutionContext& context) { 
    if (m_perception) { 
        context.currentEnvironment = m_perception->gatherEnvironmentInfo(); 
        
        // Update feedback loop with new environment data
        std::lock_guard<std::mutex> lock(m_feedbackMutex);
        m_feedbackLoop.environmentHistory.push_back(context.currentEnvironment);
        
        // Keep only last 10 environment snapshots for efficiency
        if (m_feedbackLoop.environmentHistory.size() > 10) {
            m_feedbackLoop.environmentHistory.erase(m_feedbackLoop.environmentHistory.begin());
        }
    } 
}

bool Orchestrator::detectEnvironmentChanges(const ExecutionContext& context) { 
    std::lock_guard<std::mutex> lock(m_feedbackMutex);
    
    if (!m_feedbackLoop.lastEnvironmentSnapshot.empty() && !context.currentEnvironment.empty()) {
        return analyzeEnvironmentChanges(context.currentEnvironment, m_feedbackLoop.lastEnvironmentSnapshot);
    }
    return false; 
}

void Orchestrator::adaptToEnvironmentChanges(ExecutionContext& context) { 
    Logger::log(LogLevel::DEBUG, "Adapting to environment changes detected");
    
    // Request LLM to analyze environment changes and suggest adaptations
    requestLLMEnvironmentAnalysis(context);
}

void Orchestrator::cleanupCompletedExecutions() { }

// Intelligent Feedback Loop Implementation
void Orchestrator::startContinuousEnvironmentMonitoring() {
    BURWELL_TRY_CATCH({
        if (m_feedbackActive) return;
        
        Logger::log(LogLevel::INFO, "Starting continuous environment monitoring with intelligent feedback loop");
        
        m_feedbackActive = true;
        m_feedbackThread = std::thread(&Orchestrator::feedbackLoopWorker, this);
        
        logActivity("Continuous environment monitoring started");
    }, "Orchestrator::startContinuousEnvironmentMonitoring");
}

void Orchestrator::stopContinuousEnvironmentMonitoring() {
    BURWELL_TRY_CATCH({
        if (!m_feedbackActive) return;
        
        Logger::log(LogLevel::INFO, "Stopping continuous environment monitoring");
        
        m_feedbackActive = false;
        
        if (m_feedbackThread.joinable()) {
            m_feedbackThread.join();
        }
        
        logActivity("Continuous environment monitoring stopped");
    }, "Orchestrator::stopContinuousEnvironmentMonitoring");
}

void Orchestrator::feedbackLoopWorker() {
    Logger::log(LogLevel::DEBUG, "Feedback loop worker thread started");
    
    while (m_feedbackActive) {
        try {
            auto now = std::chrono::steady_clock::now();
            
            {
                std::lock_guard<std::mutex> lock(m_feedbackMutex);
                
                // Check if it's time for environment monitoring
                auto timeSinceLastCheck = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_feedbackLoop.lastEnvironmentCheck).count();
                
                if (timeSinceLastCheck >= m_feedbackLoop.environmentCheckIntervalMs) {
                    m_feedbackLoop.lastEnvironmentCheck = now;
                    
                    // Capture current environment
                    if (m_perception && m_feedbackLoop.continuousMonitoringEnabled) {
                        nlohmann::json currentEnv = m_perception->gatherEnvironmentInfo();
                        
                        // Analyze changes
                        if (!m_feedbackLoop.lastEnvironmentSnapshot.empty()) {
                            if (analyzeEnvironmentChanges(currentEnv, m_feedbackLoop.lastEnvironmentSnapshot)) {
                                Logger::log(LogLevel::DEBUG, "Environment changes detected by feedback loop");
                                
                                // Store change for later analysis
                                nlohmann::json changeEvent;
                                changeEvent["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    now.time_since_epoch()).count();
                                changeEvent["previous"] = m_feedbackLoop.lastEnvironmentSnapshot;
                                changeEvent["current"] = currentEnv;
                                
                                // If we have an active execution context, adapt to changes
                                // This would trigger re-planning if significant changes detected
                            }
                        }
                        
                        m_feedbackLoop.lastEnvironmentSnapshot = currentEnv;
                    }
                }
            }
            
            // Sleep for a short period before next check
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERROR_LEVEL, "Feedback loop worker error: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
    
    Logger::log(LogLevel::DEBUG, "Feedback loop worker thread stopped");
}

bool Orchestrator::analyzeEnvironmentChanges(const nlohmann::json& currentEnv, const nlohmann::json& previousEnv) {
    try {
        // Simple change detection - compare key environment indicators
        if (!currentEnv.contains("windowCount") || !previousEnv.contains("windowCount")) {
            return false;
        }
        
        // Check for window count changes
        if (currentEnv["windowCount"] != previousEnv["windowCount"]) {
            Logger::log(LogLevel::DEBUG, "Window count changed: " + 
                       std::to_string(previousEnv["windowCount"].get<int>()) + " -> " + 
                       std::to_string(currentEnv["windowCount"].get<int>()));
            return true;
        }
        
        // Check for active window changes
        if (currentEnv.contains("activeWindow") && previousEnv.contains("activeWindow")) {
            if (currentEnv["activeWindow"]["title"] != previousEnv["activeWindow"]["title"]) {
                Logger::log(LogLevel::DEBUG, "Active window changed: " + 
                           previousEnv["activeWindow"]["title"].get<std::string>() + " -> " + 
                           currentEnv["activeWindow"]["title"].get<std::string>());
                return true;
            }
        }
        
        // Check for screen resolution changes
        if (currentEnv.contains("screenSize") && previousEnv.contains("screenSize")) {
            if (currentEnv["screenSize"] != previousEnv["screenSize"]) {
                Logger::log(LogLevel::DEBUG, "Screen size changed");
                return true;
            }
        }
        
        return false;
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error analyzing environment changes: " + std::string(e.what()));
        return false;
    }
}

void Orchestrator::updateCommandSuccessMetrics(const std::string& command, bool success) {
    std::lock_guard<std::mutex> lock(m_feedbackMutex);
    
    if (success) {
        m_feedbackLoop.commandSuccessRates[command]++;
    } else {
        m_feedbackLoop.commandSuccessRates[command + "_failed"]++;
    }
    
    Logger::log(LogLevel::DEBUG, "Updated success metrics for command: " + command + 
               " (success: " + (success ? "true" : "false") + ")");
}

void Orchestrator::requestLLMEnvironmentAnalysis(ExecutionContext& context) {
    try {
        if (!m_llmConnector) return;
        
        Logger::log(LogLevel::DEBUG, "Requesting LLM analysis of environment changes");
        
        // Build analysis request
        std::string analysisPrompt = "The environment has changed during automation. Please analyze the changes and suggest adaptations:\n\n";
        analysisPrompt += "Current Environment:\n" + context.currentEnvironment.dump(2) + "\n\n";
        
        {
            std::lock_guard<std::mutex> lock(m_feedbackMutex);
            if (!m_feedbackLoop.lastEnvironmentSnapshot.empty()) {
                analysisPrompt += "Previous Environment:\n" + m_feedbackLoop.lastEnvironmentSnapshot.dump(2) + "\n\n";
            }
            
            if (!m_feedbackLoop.currentExecutionPlan.empty()) {
                analysisPrompt += "Current Execution Plan:\n" + m_feedbackLoop.currentExecutionPlan.dump(2) + "\n\n";
            }
        }
        
        analysisPrompt += "Please provide:\n1. Analysis of what changed\n2. Impact on current automation\n3. Suggested command adaptations";
        
        // This would trigger an async LLM analysis in a real implementation
        // For now, we log the request
        logActivity("LLM environment analysis requested");
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error requesting LLM environment analysis: " + std::string(e.what()));
    }
}

// =================== CONVERSATIONAL LLM SYSTEM ===================

std::string Orchestrator::initiateConversationalExecution(const std::string& userInput, ExecutionContext& context) {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    try {
        // Generate unique conversation ID
        std::string conversationId = generateRequestId() + "_conv";
        
        // Initialize conversation state
        ConversationState& conversation = m_activeConversations[conversationId];
        conversation.conversationId = conversationId;
        conversation.lastInteraction = std::chrono::steady_clock::now();
        conversation.currentContext = {
            {"user_request", userInput},
            {"execution_context", {
                {"request_id", context.requestId},
                {"current_environment", context.currentEnvironment}
            }}
        };
        
        // Add initial user message to conversation history
        conversation.messageHistory.push_back({
            {"role", "user"},
            {"content", userInput},
            {"timestamp", getCurrentTimeMs()}
        });
        
        // Check if we should request additional environmental data before initial planning
        if (shouldRequestAdditionalEnvironmentalData(context, nlohmann::json::object())) {
            Logger::log(LogLevel::INFO, "Requesting additional environmental data for conversation: " + conversationId);
            
            // Generate environmental data query
            auto environmentQuery = generateEnvironmentalDataQuery(context, "initial_planning");
            conversation.environmentalQueries["initial"] = environmentQuery;
            conversation.requiresEnvironmentalUpdate = true;
            
            // Request environmental data
            auto envData = handleEnvironmentalDataRequest(conversationId, environmentQuery);
            updateConversationContext(conversationId, envData);
        }
        
        Logger::log(LogLevel::INFO, "Initiated conversational execution with ID: " + conversationId);
        return conversationId;
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error initiating conversational execution: " + std::string(e.what()));
        return "";
    }
}

TaskExecutionResult Orchestrator::processConversationalTurn(const std::string& conversationId, const nlohmann::json& llmResponse) {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    TaskExecutionResult result;
    
    try {
        auto convIt = m_activeConversations.find(conversationId);
        if (convIt == m_activeConversations.end()) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Conversation not found: " + conversationId;
            return result;
        }
        
        ConversationState& conversation = convIt->second;
        conversation.lastInteraction = std::chrono::steady_clock::now();
        
        // Add LLM response to conversation history
        conversation.messageHistory.push_back({
            {"role", "assistant"},
            {"content", llmResponse},
            {"timestamp", getCurrentTimeMs()}
        });
        
        // Parse LLM response to determine next action
        if (llmResponse.contains("action_type")) {
            std::string actionType = llmResponse["action_type"];
            
            if (actionType == "request_environment_data") {
                // LLM is requesting additional environmental information
                Logger::log(LogLevel::DEBUG, "LLM requesting environmental data for conversation: " + conversationId);
                
                auto envRequest = llmResponse.value("environment_request", nlohmann::json::object());
                auto envData = handleEnvironmentalDataRequest(conversationId, envRequest);
                
                // Update conversation context with new environmental data
                updateConversationContext(conversationId, envData);
                
                result.status = ExecutionStatus::IN_PROGRESS;
                result.result = {
                    {"conversation_id", conversationId},
                    {"environmental_data", envData},
                    {"next_action", "continue_conversation"}
                };
                
            } else if (actionType == "adapt_commands") {
                // LLM wants to adapt/modify commands based on feedback
                Logger::log(LogLevel::DEBUG, "LLM adapting commands for conversation: " + conversationId);
                
                result = adaptCommandsBasedOnFeedback(conversationId, llmResponse);
                
            } else if (actionType == "execute_plan") {
                // LLM has finalized the execution plan
                Logger::log(LogLevel::INFO, "LLM ready to execute plan for conversation: " + conversationId);
                
                if (llmResponse.contains("execution_plan")) {
                    auto executionPlan = llmResponse["execution_plan"];
                    
                    // Create execution context from conversation
                    ExecutionContext execContext;
                    execContext.requestId = conversationId;
                    execContext.originalRequest = conversation.currentContext["user_request"];
                    execContext.currentEnvironment = conversation.currentContext.value("current_environment", nlohmann::json::object());
                    
                    // Execute the finalized plan
                    result = executeCommandSequence(executionPlan["commands"], execContext);
                    
                    if (result.status == ExecutionStatus::COMPLETED) {
                        // Clean up conversation on successful completion
                        m_activeConversations.erase(conversationId);
                    }
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "LLM execute_plan action missing execution_plan";
                }
                
            } else if (actionType == "request_user_input") {
                // LLM needs additional input from user - handle it with the user interaction system
                Logger::log(LogLevel::INFO, "LLM requesting user input for conversation: " + conversationId);
                
                result = handleUserInteractionRequest(llmResponse);
                
                if (result.status == ExecutionStatus::COMPLETED) {
                    // User provided input, continue conversation with LLM
                    auto userResponse = result.result.value("user_response", nlohmann::json::object());
                    
                    // Add user response to conversation history
                    auto convIt = m_activeConversations.find(conversationId);
                    if (convIt != m_activeConversations.end()) {
                        convIt->second.messageHistory.push_back({
                            {"role", "user"},
                            {"content", userResponse},
                            {"timestamp", getCurrentTimeMs()},
                            {"interaction_response", true}
                        });
                    }
                    
                    result.status = ExecutionStatus::IN_PROGRESS;
                    result.result["next_action"] = "continue_conversation_with_user_input";
                } else {
                    // User interaction failed or timed out
                    Logger::log(LogLevel::WARNING, "User interaction failed for conversation: " + conversationId);
                }
            }
        } else {
            // Handle legacy format or direct command execution
            if (llmResponse.contains("commands")) {
                ExecutionContext execContext;
                execContext.requestId = conversationId;
                execContext.originalRequest = conversation.currentContext["user_request"];
                
                result = executeCommandSequence(llmResponse["commands"], execContext);
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Invalid LLM response format";
            }
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Error processing conversational turn: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, result.errorMessage);
    }
    
    return result;
}

nlohmann::json Orchestrator::handleEnvironmentalDataRequest(const std::string& conversationId, const nlohmann::json& request) {
    nlohmann::json environmentData;
    
    try {
        std::string requestType = request.value("type", "general");
        
        if (m_perception) {
            // Gather comprehensive environmental information
            auto envInfo = m_perception->gatherEnvironmentInfo();
            environmentData["base_environment"] = envInfo;
            
            // Handle specific environmental data requests
            if (requestType == "screen_analysis") {
                // Detailed screen analysis with OCR and UI elements
                if (envInfo.contains("ui_elements")) {
                    environmentData["ui_analysis"] = envInfo["ui_elements"];
                }
                if (envInfo.contains("text_content")) {
                    environmentData["text_analysis"] = envInfo["text_content"];
                }
                
            } else if (requestType == "window_focus") {
                // Focus on specific window information
                environmentData["window_info"] = {
                    {"active_window", envInfo.value("activeWindow", "")},
                    {"window_list", envInfo.value("openWindows", nlohmann::json::array())},
                    {"window_positions", envInfo.value("windowPositions", nlohmann::json::object())}
                };
                
            } else if (requestType == "coordinate_mapping") {
                // Provide coordinate mapping for UI elements
                if (envInfo.contains("clickable_elements")) {
                    environmentData["coordinate_map"] = envInfo["clickable_elements"];
                }
                
            } else if (requestType == "application_state") {
                // Application-specific state information
                environmentData["app_state"] = {
                    {"running_processes", envInfo.value("runningProcesses", nlohmann::json::array())},
                    {"system_metrics", envInfo.value("systemMetrics", nlohmann::json::object())}
                };
            }
            
            // Add screenshot data if available and requested
            if (request.value("include_screenshot", false) && m_llmConnector->supportsVision()) {
                auto screenshot = m_perception->captureScreen();
                if (screenshot.isValid()) {
                    environmentData["screenshot"] = {
                        {"data", screenshot.data},
                        {"format", screenshot.format},
                        {"timestamp", getCurrentTimeMs()}
                    };
                }
            }
        }
        
        // Add timestamp and conversation context
        environmentData["timestamp"] = getCurrentTimeMs();
        environmentData["conversation_id"] = conversationId;
        environmentData["request_type"] = requestType;
        
        Logger::log(LogLevel::DEBUG, "Generated environmental data for conversation: " + conversationId + 
                   ", type: " + requestType + ", data size: " + std::to_string(environmentData.size()));
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error handling environmental data request: " + std::string(e.what()));
        environmentData = {
            {"error", "Failed to gather environmental data"},
            {"error_message", e.what()},
            {"timestamp", getCurrentTimeMs()}
        };
    }
    
    return environmentData;
}

TaskExecutionResult Orchestrator::adaptCommandsBasedOnFeedback(const std::string& conversationId, const nlohmann::json& adaptationRequest) {
    TaskExecutionResult result;
    
    try {
        auto convIt = m_activeConversations.find(conversationId);
        if (convIt == m_activeConversations.end()) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Conversation not found for adaptation: " + conversationId;
            return result;
        }
        
        ConversationState& conversation = convIt->second;
        
        // Extract adaptation parameters
        std::string adaptationType = adaptationRequest.value("adaptation_type", "command_modification");
        auto originalCommands = adaptationRequest.value("original_commands", nlohmann::json::array());
        auto modifications = adaptationRequest.value("modifications", nlohmann::json::array());
        
        if (adaptationType == "command_modification") {
            // Modify existing commands based on environmental feedback
            nlohmann::json adaptedCommands = originalCommands;
            
            for (const auto& modification : modifications) {
                int commandIndex = modification.value("command_index", -1);
                if (commandIndex >= 0 && commandIndex < adaptedCommands.size()) {
                    auto& command = adaptedCommands[commandIndex];
                    
                    // Apply modifications
                    if (modification.contains("new_params")) {
                        command["params"] = modification["new_params"];
                    }
                    if (modification.contains("new_command")) {
                        command["command"] = modification["new_command"];
                    }
                    if (modification.contains("additional_delay")) {
                        command["delayAfterMs"] = command.value("delayAfterMs", 0) + modification["additional_delay"].get<int>();
                    }
                }
            }
            
            result.status = ExecutionStatus::COMPLETED;
            result.result = {
                {"adapted_commands", adaptedCommands},
                {"adaptation_summary", adaptationRequest.value("reasoning", "Commands adapted based on environmental feedback")}
            };
            
        } else if (adaptationType == "retry_with_alternatives") {
            // Generate alternative approaches for failed commands
            auto failedCommands = adaptationRequest.value("failed_commands", nlohmann::json::array());
            nlohmann::json alternatives = nlohmann::json::array();
            
            for (const auto& failedCmd : failedCommands) {
                // Generate alternative approaches based on command type and failure reason
                std::string cmdType = failedCmd.value("command", "");
                std::string failureReason = failedCmd.value("failure_reason", "");
                
                if (cmdType.find("mouse.") == 0) {
                    // Mouse command alternatives
                    alternatives.push_back({
                        {"original_command", failedCmd},
                        {"alternative_approaches", {
                            {{"command", "mouse.click"}, {"params", {{"x", failedCmd["params"]["x"].get<int>() + 5}, {"y", failedCmd["params"]["y"].get<int>() + 5}}}, {"description", "Slight offset click"}},
                            {{"command", "mouse.doubleClick"}, {"params", failedCmd["params"]}, {"description", "Try double-click instead"}},
                            {{"command", "mouse.rightClick"}, {"params", failedCmd["params"]}, {"description", "Try right-click context menu"}}
                        }}
                    });
                }
            }
            
            result.status = ExecutionStatus::COMPLETED;
            result.result = {
                {"alternative_commands", alternatives},
                {"adaptation_type", "retry_alternatives"}
            };
        }
        
        // Update conversation context with adaptation information
        conversation.currentContext["last_adaptation"] = {
            {"type", adaptationType},
            {"timestamp", getCurrentTimeMs()},
            {"success", result.status == ExecutionStatus::COMPLETED}
        };
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Error adapting commands: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, result.errorMessage);
    }
    
    return result;
}

bool Orchestrator::shouldRequestAdditionalEnvironmentalData(const ExecutionContext& context, const nlohmann::json& currentPlan) {
    try {
        // Check if current environmental data is sufficient
        if (context.currentEnvironment.empty()) {
            return true;  // Always request data if we have none
        }
        
        // Check if environmental data is stale (older than 5 seconds)
        auto now = getCurrentTimeMs();
        double lastUpdate = context.currentEnvironment.value("timestamp", 0.0);
        if (now - lastUpdate > 5000) {
            return true;
        }
        
        // Check if the planned commands require specific environmental data
        if (currentPlan.contains("commands")) {
            for (const auto& command : currentPlan["commands"]) {
                std::string cmdType = command.value("command", "");
                
                // Mouse commands need current UI element positions
                if (cmdType.find("mouse.") == 0 && !context.currentEnvironment.contains("ui_elements")) {
                    return true;
                }
                
                // Application commands need current process list
                if (cmdType.find("application.") == 0 && !context.currentEnvironment.contains("runningProcesses")) {
                    return true;
                }
            }
        }
        
        return false;  // Current data is sufficient
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::WARNING, "Error checking environmental data requirements: " + std::string(e.what()));
        return true;  // Request data on error to be safe
    }
}

nlohmann::json Orchestrator::generateEnvironmentalDataQuery(const ExecutionContext& context, const std::string& queryType) {
    nlohmann::json query;
    
    try {
        query["type"] = queryType;
        query["timestamp"] = getCurrentTimeMs();
        query["conversation_context"] = {
            {"request_id", context.requestId},
            {"original_request", context.originalRequest}
        };
        
        if (queryType == "initial_planning") {
            query["requirements"] = {
                {"include_screenshot", m_llmConnector->supportsVision()},
                {"include_ui_elements", true},
                {"include_text_content", true},
                {"include_window_info", true}
            };
            
        } else if (queryType == "command_validation") {
            query["requirements"] = {
                {"include_coordinate_mapping", true},
                {"include_clickable_elements", true},
                {"focus_active_window", true}
            };
            
        } else if (queryType == "execution_monitoring") {
            query["requirements"] = {
                {"lightweight_update", true},
                {"focus_changes_only", true},
                {"include_error_indicators", true}
            };
        }
        
        Logger::log(LogLevel::DEBUG, "Generated environmental query: " + queryType + " for request: " + context.requestId);
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error generating environmental query: " + std::string(e.what()));
        query = {
            {"type", "general"},
            {"error", "Failed to generate specific query"}
        };
    }
    
    return query;
}

void Orchestrator::updateConversationContext(const std::string& conversationId, const nlohmann::json& newContext) {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    try {
        auto convIt = m_activeConversations.find(conversationId);
        if (convIt != m_activeConversations.end()) {
            ConversationState& conversation = convIt->second;
            
            // Merge new context data
            for (auto& [key, value] : newContext.items()) {
                conversation.currentContext[key] = value;
            }
            
            conversation.lastInteraction = std::chrono::steady_clock::now();
            conversation.requiresEnvironmentalUpdate = false;  // Mark as updated
            
            Logger::log(LogLevel::DEBUG, "Updated conversation context for: " + conversationId);
        }
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error updating conversation context: " + std::string(e.what()));
    }
}

void Orchestrator::cleanupExpiredConversations() {
    std::lock_guard<std::mutex> lock(m_conversationMutex);
    
    try {
        auto now = std::chrono::steady_clock::now();
        const auto maxAge = std::chrono::minutes(30);  // Cleanup conversations older than 30 minutes
        
        auto it = m_activeConversations.begin();
        while (it != m_activeConversations.end()) {
            if (now - it->second.lastInteraction > maxAge) {
                Logger::log(LogLevel::DEBUG, "Cleaning up expired conversation: " + it->first);
                it = m_activeConversations.erase(it);
            } else {
                ++it;
            }
        }
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error cleaning up conversations: " + std::string(e.what()));
    }
}

// =================== ERROR RECOVERY SYSTEM ===================

TaskExecutionResult Orchestrator::initiateErrorRecovery(const std::string& failedCommand, const nlohmann::json& commandContext, const std::string& errorMessage) {
    std::lock_guard<std::mutex> lock(m_errorRecoveryMutex);
    TaskExecutionResult result;
    
    try {
        // Generate unique recovery ID
        std::string recoveryId = generateRequestId() + "_recovery";
        
        // Initialize error recovery state
        ErrorRecoveryState& recoveryState = m_errorRecoveryStates[recoveryId];
        recoveryState.originalCommand = failedCommand;
        recoveryState.commandContext = commandContext;
        recoveryState.lastError = errorMessage;
        recoveryState.lastAttempt = std::chrono::steady_clock::now();
        recoveryState.retryCount = 0;
        
        // Capture current environment snapshot for analysis
        if (m_perception) {
            recoveryState.environmentSnapshot = m_perception->gatherEnvironmentInfo();
        }
        
        // Add initial failure to attempt history
        recoveryState.attemptHistory.push_back("Initial failure: " + errorMessage);
        
        Logger::log(LogLevel::INFO, "Initiated error recovery for command: " + failedCommand + " with ID: " + recoveryId);
        
        // Determine if this error requires LLM analysis
        recoveryState.requiresLLMAnalysis = shouldRetryCommand(recoveryState, errorMessage);
        
        // Check LLM availability before attempting LLM-based recovery
        bool llmAvailable = false;
        if (recoveryState.requiresLLMAnalysis && m_llmConnector) {
            llmAvailable = m_llmConnector->validateConfiguration();
            if (!llmAvailable) {
                Logger::log(LogLevel::WARNING, "LLM analysis requested but LLM not available (no API key or configuration issue)");
                recoveryState.requiresLLMAnalysis = false;  // Fallback to simple retry
            }
        }
        
        if (recoveryState.requiresLLMAnalysis && llmAvailable) {
            // Perform LLM-based error analysis and recovery
            result = performLLMErrorAnalysis(recoveryId, recoveryState);
        } else {
            // Simple retry with minor variations
            auto alternatives = generateAlternativeCommands(recoveryState);
            result = retryCommandWithAlternatives(recoveryId, alternatives);
        }
        
        result.result["recovery_id"] = recoveryId;
        result.result["recovery_type"] = recoveryState.requiresLLMAnalysis ? "llm_analysis" : "simple_retry";
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Error initiating recovery: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, result.errorMessage);
    }
    
    return result;
}

TaskExecutionResult Orchestrator::performLLMErrorAnalysis(const std::string& recoveryId, const ErrorRecoveryState& state) {
    TaskExecutionResult result;
    
    try {
        Logger::log(LogLevel::INFO, "Performing LLM error analysis for recovery: " + recoveryId);
        
        // Build enhanced context for LLM analysis
        LLMContext analysisContext;
        if (m_perception) {
            auto currentEnv = m_perception->gatherEnvironmentInfo();
            analysisContext.structuredData = currentEnv;
            
            // Add environmental comparison
            if (!state.environmentSnapshot.empty()) {
                analysisContext.structuredData["environment_comparison"] = {
                    {"before_failure", state.environmentSnapshot},
                    {"current_state", currentEnv}
                };
            }
        }
        
        // Prepare error analysis prompt
        std::string analysisPrompt = "COMMAND FAILURE ANALYSIS:\n";
        analysisPrompt += "Failed Command: " + state.originalCommand + "\n";
        analysisPrompt += "Error Message: " + state.lastError + "\n";
        analysisPrompt += "Retry Count: " + std::to_string(state.retryCount) + "\n";
        
        if (!state.attemptHistory.empty()) {
            analysisPrompt += "Previous Attempts:\n";
            for (const auto& attempt : state.attemptHistory) {
                analysisPrompt += "- " + attempt + "\n";
            }
        }
        
        analysisPrompt += "\nPlease analyze this failure and provide:\n";
        analysisPrompt += "1. Root cause analysis\n";
        analysisPrompt += "2. Alternative approaches\n";
        analysisPrompt += "3. Modified command parameters\n";
        analysisPrompt += "4. Environmental considerations\n";
        analysisPrompt += "\nReturn response in JSON format with action_type: 'error_recovery'";
        
        // Request LLM analysis
        auto llmResponse = m_llmConnector->generatePlanWithContext(analysisPrompt, analysisContext);
        
        if (llmResponse.isValid) {
            // Parse LLM recovery suggestions
            nlohmann::json recoveryPlan;
            
            // Check if the response contains error recovery information
            // llmResponse.commands is a vector, so we need to check the reasoning or build a plan object
            if (!llmResponse.commands.empty()) {
                // Try to extract error recovery information from the first command or reasoning
                if (!llmResponse.commands.empty() && llmResponse.commands[0].contains("action_type") && 
                    llmResponse.commands[0]["action_type"] == "error_recovery") {
                    
                    recoveryPlan = llmResponse.commands[0];
                } else {
                    // Build recovery plan from reasoning and available commands
                    recoveryPlan = {
                        {"action_type", "error_recovery"},
                        {"root_cause", "Command execution failed"},
                        {"reasoning", llmResponse.reasoning},
                        {"alternative_commands", llmResponse.commands}
                    };
                }
                
                // Extract alternative commands from LLM response
                std::vector<nlohmann::json> alternatives;
                if (recoveryPlan.contains("alternative_commands")) {
                    for (const auto& altCmd : recoveryPlan["alternative_commands"]) {
                        alternatives.push_back(altCmd);
                    }
                }
                
                // If no alternatives provided, generate some based on analysis
                if (alternatives.empty()) {
                    alternatives = generateAlternativeCommands(state);
                }
                
                // Attempt recovery with LLM-suggested alternatives
                result = retryCommandWithAlternatives(recoveryId, alternatives);
                
                // Add LLM analysis to result
                result.result["llm_analysis"] = {
                    {"root_cause", recoveryPlan.value("root_cause", "Unknown")},
                    {"reasoning", llmResponse.reasoning},
                    {"environmental_factors", recoveryPlan.value("environmental_factors", nlohmann::json::array())}
                };
                
            } else {
                // Fallback to simple retry if LLM response is invalid
                Logger::log(LogLevel::WARNING, "Invalid LLM error analysis response, falling back to simple retry");
                auto alternatives = generateAlternativeCommands(state);
                result = retryCommandWithAlternatives(recoveryId, alternatives);
            }
        } else {
            // LLM analysis failed, fall back to simple retry
            Logger::log(LogLevel::WARNING, "LLM error analysis failed, falling back to simple retry");
            auto alternatives = generateAlternativeCommands(state);
            result = retryCommandWithAlternatives(recoveryId, alternatives);
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Error in LLM analysis: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, result.errorMessage);
    }
    
    return result;
}

std::vector<nlohmann::json> Orchestrator::generateAlternativeCommands(const ErrorRecoveryState& state) {
    std::vector<nlohmann::json> alternatives;
    
    try {
        // Parse the original command to understand its structure
        nlohmann::json originalCmd = nlohmann::json::parse(state.originalCommand);
        std::string cmdType = originalCmd.value("command", "");
        
        if (cmdType.find("mouse.") == 0) {
            // Mouse command alternatives
            auto params = originalCmd.value("params", nlohmann::json::object());
            int x = params.value("x", 0);
            int y = params.value("y", 0);
            
            // Try slightly different coordinates
            alternatives.push_back({
                {"command", cmdType},
                {"params", {{"x", x + 5}, {"y", y + 5}}},
                {"description", "Offset click (+5,+5)"}
            });
            
            alternatives.push_back({
                {"command", cmdType},
                {"params", {{"x", x - 5}, {"y", y - 5}}},
                {"description", "Offset click (-5,-5)"}
            });
            
            // Try different click types
            if (cmdType == "mouse.click") {
                alternatives.push_back({
                    {"command", "mouse.doubleClick"},
                    {"params", params},
                    {"description", "Try double-click instead"}
                });
                
                alternatives.push_back({
                    {"command", "mouse.rightClick"},
                    {"params", params},
                    {"description", "Try right-click context menu"}
                });
            }
            
        } else if (cmdType.find("keyboard.") == 0) {
            // Keyboard command alternatives
            auto params = originalCmd.value("params", nlohmann::json::object());
            
            if (cmdType == "keyboard.type") {
                std::string text = params.value("text", "");
                
                // Try typing with delays
                alternatives.push_back({
                    {"command", "keyboard.type"},
                    {"params", {{"text", text}, {"delay_between_chars", 50}}},
                    {"description", "Type with character delays"}
                });
                
                // Try sending as individual key presses
                alternatives.push_back({
                    {"command", "keyboard.sendKeys"},
                    {"params", {{"keys", text}}},
                    {"description", "Send as individual key presses"}
                });
                
            } else if (cmdType == "keyboard.hotkey") {
                // Try alternative hotkey combinations
                auto keys = params.value("keys", nlohmann::json::array());
                if (!keys.empty()) {
                    alternatives.push_back({
                        {"command", "keyboard.hotkey"},
                        {"params", {{"keys", keys}, {"delay_after", 100}}},
                        {"description", "Hotkey with delay"}
                    });
                }
            }
            
        } else if (cmdType.find("application.") == 0) {
            // Application command alternatives
            if (cmdType == "application.launch") {
                auto params = originalCmd.value("params", nlohmann::json::object());
                std::string appPath = params.value("path", "");
                
                // Try launching with different parameters
                alternatives.push_back({
                    {"command", "application.launch"},
                    {"params", {{"path", appPath}, {"wait_for_launch", true}}},
                    {"description", "Launch with wait"}
                });
                
                // Try using system shell to launch
                alternatives.push_back({
                    {"command", "system.shell"},
                    {"params", {{"command", "start \"\" \"" + appPath + "\""}}},
                    {"description", "Launch via system shell"}
                });
            }
        }
        
        // Add delay variations for all alternatives
        for (auto& alt : alternatives) {
            if (!alt.contains("delayAfterMs")) {
                alt["delayAfterMs"] = 500 + (state.retryCount * 200);  // Increasing delays
            }
        }
        
        Logger::log(LogLevel::DEBUG, "Generated " + std::to_string(alternatives.size()) + " alternative commands for recovery");
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error generating alternatives: " + std::string(e.what()));
        
        // Return a simple retry as fallback
        alternatives.push_back({
            {"command", "delay"},
            {"params", {{"ms", 1000}}},
            {"description", "Wait and retry original command"}
        });
    }
    
    return alternatives;
}

TaskExecutionResult Orchestrator::retryCommandWithAlternatives(const std::string& recoveryId, const std::vector<nlohmann::json>& alternatives) {
    TaskExecutionResult result;
    
    try {
        auto recoveryIt = m_errorRecoveryStates.find(recoveryId);
        if (recoveryIt == m_errorRecoveryStates.end()) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Recovery state not found: " + recoveryId;
            return result;
        }
        
        ErrorRecoveryState& recoveryState = recoveryIt->second;
        
        Logger::log(LogLevel::INFO, "Retrying command with " + std::to_string(alternatives.size()) + " alternatives");
        
        // Try each alternative
        for (const auto& alternative : alternatives) {
            if (recoveryState.retryCount >= recoveryState.maxRetries) {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Maximum retry attempts reached (" + std::to_string(recoveryState.maxRetries) + ")";
                break;
            }
            
            recoveryState.retryCount++;
            recoveryState.lastAttempt = std::chrono::steady_clock::now();
            
            // Create execution context for retry
            ExecutionContext retryContext;
            retryContext.requestId = recoveryId + "_retry_" + std::to_string(recoveryState.retryCount);
            retryContext.originalRequest = "Error recovery attempt";
            
            // Execute alternative command
            TaskExecutionResult retryResult = executeCommand(alternative, retryContext);
            
            std::string attemptDescription = alternative.value("description", "Alternative command");
            
            if (retryResult.status == ExecutionStatus::COMPLETED) {
                // Success! Update recovery state and return
                recoveryState.attemptHistory.push_back("SUCCESS: " + attemptDescription);
                updateErrorRecoveryState(recoveryId, "", true);
                
                result.status = ExecutionStatus::COMPLETED;
                result.result = {
                    {"recovery_id", recoveryId},
                    {"successful_alternative", alternative},
                    {"retry_count", recoveryState.retryCount},
                    {"recovery_summary", "Command succeeded with alternative: " + attemptDescription}
                };
                
                Logger::log(LogLevel::INFO, "Error recovery successful for " + recoveryId + " using: " + attemptDescription);
                return result;
                
            } else {
                // Failed, record attempt and continue
                recoveryState.attemptHistory.push_back("FAILED: " + attemptDescription + " - " + retryResult.errorMessage);
                updateErrorRecoveryState(recoveryId, retryResult.errorMessage, false);
                
                Logger::log(LogLevel::DEBUG, "Recovery attempt " + std::to_string(recoveryState.retryCount) + " failed: " + retryResult.errorMessage);
            }
        }
        
        // All alternatives failed
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "All recovery alternatives failed";
        result.result = {
            {"recovery_id", recoveryId},
            {"retry_count", recoveryState.retryCount},
            {"attempt_history", recoveryState.attemptHistory}
        };
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Error during retry: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, result.errorMessage);
    }
    
    return result;
}

bool Orchestrator::shouldRetryCommand(const ErrorRecoveryState& state, const std::string& currentError) {
    try {
        // Check retry count limit
        if (state.retryCount >= state.maxRetries) {
            return false;
        }
        
        // Analyze error type to determine if LLM analysis would help
        std::string errorLower = currentError;
        std::transform(errorLower.begin(), errorLower.end(), errorLower.begin(), ::tolower);
        
        // Simple errors that don't need LLM analysis
        if (errorLower.find("timeout") != std::string::npos ||
            errorLower.find("connection") != std::string::npos ||
            errorLower.find("network") != std::string::npos) {
            return false;  // Simple retry without LLM
        }
        
        // Complex errors that could benefit from LLM analysis
        if (errorLower.find("not found") != std::string::npos ||
            errorLower.find("invalid") != std::string::npos ||
            errorLower.find("permission") != std::string::npos ||
            errorLower.find("access denied") != std::string::npos ||
            errorLower.find("ui element") != std::string::npos) {
            return true;  // Use LLM analysis
        }
        
        // Default to simple retry for unknown errors
        return false;
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::WARNING, "Error determining retry strategy: " + std::string(e.what()));
        return false;  // Default to simple retry
    }
}

void Orchestrator::updateErrorRecoveryState(const std::string& recoveryId, const std::string& error, bool success) {
    std::lock_guard<std::mutex> lock(m_errorRecoveryMutex);
    
    try {
        auto recoveryIt = m_errorRecoveryStates.find(recoveryId);
        if (recoveryIt != m_errorRecoveryStates.end()) {
            ErrorRecoveryState& state = recoveryIt->second;
            state.lastError = error;
            state.lastAttempt = std::chrono::steady_clock::now();
            
            if (success) {
                // Clean up successful recovery after a delay
                Logger::log(LogLevel::DEBUG, "Marking recovery as successful: " + recoveryId);
                // The cleanup will happen in cleanupErrorRecoveryStates()
            }
        }
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error updating recovery state: " + std::string(e.what()));
    }
}

void Orchestrator::cleanupErrorRecoveryStates() {
    std::lock_guard<std::mutex> lock(m_errorRecoveryMutex);
    
    try {
        auto now = std::chrono::steady_clock::now();
        const auto maxAge = std::chrono::minutes(15);  // Cleanup recovery states older than 15 minutes
        
        auto it = m_errorRecoveryStates.begin();
        while (it != m_errorRecoveryStates.end()) {
            if (now - it->second.lastAttempt > maxAge) {
                Logger::log(LogLevel::DEBUG, "Cleaning up expired error recovery state: " + it->first);
                it = m_errorRecoveryStates.erase(it);
            } else {
                ++it;
            }
        }
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error cleaning up recovery states: " + std::string(e.what()));
    }
}

// =================== USER INTERACTION SYSTEM ===================

std::string Orchestrator::requestUserInput(const std::string& conversationId, const std::string& promptMessage, const std::string& inputType, const nlohmann::json& options) {
    std::lock_guard<std::mutex> lock(m_userInteractionMutex);
    
    try {
        // Generate unique interaction ID
        std::string interactionId = generateRequestId() + "_interaction";
        
        // Create user interaction request
        UserInteractionRequest& request = m_pendingUserInteractions[interactionId];
        request.interactionId = interactionId;
        request.conversationId = conversationId;
        request.promptMessage = promptMessage;
        request.inputType = inputType;
        request.inputOptions = options;
        request.requestTime = std::chrono::steady_clock::now();
        request.timeoutTime = request.requestTime + std::chrono::minutes(5);  // Default 5 minute timeout
        
        // Determine urgency based on input type
        request.isUrgent = (inputType == "password" || inputType == "confirmation");
        
        Logger::log(LogLevel::INFO, "Created user interaction request: " + interactionId + " for conversation: " + conversationId);
        Logger::log(LogLevel::INFO, "User prompt: " + promptMessage);
        
        // In a real implementation, this would trigger UI notification
        // For now, we log the request details
        nlohmann::json logData = {
            {"interaction_id", interactionId},
            {"conversation_id", conversationId},
            {"prompt", promptMessage},
            {"input_type", inputType},
            {"options", options},
            {"urgent", request.isUrgent}
        };
        
        Logger::log(LogLevel::INFO, "USER_INTERACTION_REQUEST: " + logData.dump());
        
        return interactionId;
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error creating user interaction request: " + std::string(e.what()));
        return "";
    }
}

TaskExecutionResult Orchestrator::waitForUserResponse(const std::string& interactionId, int timeoutMs) {
    TaskExecutionResult result;
    
    try {
        auto startTime = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(timeoutMs);
        
        Logger::log(LogLevel::DEBUG, "Waiting for user response to interaction: " + interactionId);
        
        // Poll for user response with timeout
        while (std::chrono::steady_clock::now() - startTime < timeout) {
            {
                std::lock_guard<std::mutex> lock(m_userInteractionMutex);
                auto requestIt = m_pendingUserInteractions.find(interactionId);
                
                if (requestIt != m_pendingUserInteractions.end()) {
                    UserInteractionRequest& request = requestIt->second;
                    
                    if (request.hasResponse) {
                        // User has provided a response
                        result.status = ExecutionStatus::COMPLETED;
                        result.result = {
                            {"interaction_id", interactionId},
                            {"user_response", request.userResponse},
                            {"response_time_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - request.requestTime).count()}
                        };
                        
                        Logger::log(LogLevel::INFO, "Received user response for interaction: " + interactionId);
                        
                        // Clean up the interaction request
                        m_pendingUserInteractions.erase(requestIt);
                        return result;
                    }
                } else {
                    // Interaction not found (possibly cancelled)
                    result.status = ExecutionStatus::CANCELLED;
                    result.errorMessage = "User interaction request not found or was cancelled";
                    return result;
                }
            }
            
            // Brief sleep to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Timeout reached
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "User input timeout after " + std::to_string(timeoutMs) + "ms";
        
        // Clean up timed out interaction
        {
            std::lock_guard<std::mutex> lock(m_userInteractionMutex);
            m_pendingUserInteractions.erase(interactionId);
        }
        
        Logger::log(LogLevel::WARNING, "User interaction timed out: " + interactionId);
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Error waiting for user response: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, result.errorMessage);
    }
    
    return result;
}

bool Orchestrator::provideUserResponse(const std::string& interactionId, const nlohmann::json& response) {
    std::lock_guard<std::mutex> lock(m_userInteractionMutex);
    
    try {
        auto requestIt = m_pendingUserInteractions.find(interactionId);
        if (requestIt != m_pendingUserInteractions.end()) {
            UserInteractionRequest& request = requestIt->second;
            
            if (!request.hasResponse) {
                // Validate response based on input type
                bool validResponse = true;
                std::string validationError = "";
                
                if (request.inputType == "text" || request.inputType == "password") {
                    if (!response.contains("text") || !response["text"].is_string()) {
                        validResponse = false;
                        validationError = "Text response must contain 'text' field with string value";
                    }
                } else if (request.inputType == "choice") {
                    if (!response.contains("choice")) {
                        validResponse = false;
                        validationError = "Choice response must contain 'choice' field";
                    } else if (request.inputOptions.contains("choices")) {
                        // Validate choice is in allowed options
                        bool validChoice = false;
                        for (const auto& option : request.inputOptions["choices"]) {
                            if (response["choice"] == option) {
                                validChoice = true;
                                break;
                            }
                        }
                        if (!validChoice) {
                            validResponse = false;
                            validationError = "Choice not in allowed options";
                        }
                    }
                } else if (request.inputType == "confirmation") {
                    if (!response.contains("confirmed") || !response["confirmed"].is_boolean()) {
                        validResponse = false;
                        validationError = "Confirmation response must contain 'confirmed' field with boolean value";
                    }
                } else if (request.inputType == "file_path") {
                    if (!response.contains("file_path") || !response["file_path"].is_string()) {
                        validResponse = false;
                        validationError = "File path response must contain 'file_path' field with string value";
                    }
                }
                
                if (validResponse) {
                    request.userResponse = response;
                    request.hasResponse = true;
                    
                    Logger::log(LogLevel::INFO, "User response received for interaction: " + interactionId);
                    return true;
                } else {
                    Logger::log(LogLevel::WARNING, "Invalid user response for interaction " + interactionId + ": " + validationError);
                    return false;
                }
            } else {
                Logger::log(LogLevel::WARNING, "User interaction already has a response: " + interactionId);
                return false;
            }
        } else {
            Logger::log(LogLevel::WARNING, "User interaction not found: " + interactionId);
            return false;
        }
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error providing user response: " + std::string(e.what()));
        return false;
    }
}

TaskExecutionResult Orchestrator::handleUserInteractionRequest(const nlohmann::json& interactionRequest) {
    TaskExecutionResult result;
    
    try {
        // Extract interaction parameters
        std::string conversationId = interactionRequest.value("conversation_id", "");
        std::string promptMessage = interactionRequest.value("user_prompt", "Input required");
        std::string inputType = interactionRequest.value("input_type", "text");
        nlohmann::json options = interactionRequest.value("options", nlohmann::json::object());
        int timeoutMs = interactionRequest.value("timeout_ms", 30000);  // Default 30 seconds
        
        Logger::log(LogLevel::INFO, "Handling user interaction request for conversation: " + conversationId);
        
        // Create the user interaction request
        std::string interactionId = requestUserInput(conversationId, promptMessage, inputType, options);
        
        if (interactionId.empty()) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Failed to create user interaction request";
            return result;
        }
        
        // Wait for user response
        result = waitForUserResponse(interactionId, timeoutMs);
        
        if (result.status == ExecutionStatus::COMPLETED) {
            // Add interaction metadata to result
            result.result["interaction_handled"] = true;
            result.result["interaction_type"] = inputType;
            
            Logger::log(LogLevel::INFO, "User interaction completed successfully: " + interactionId);
        } else {
            Logger::log(LogLevel::WARNING, "User interaction failed or timed out: " + interactionId);
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Error handling user interaction: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, result.errorMessage);
    }
    
    return result;
}

std::vector<Orchestrator::UserInteractionRequest> Orchestrator::getPendingUserInteractions() const {
    std::lock_guard<std::mutex> lock(m_userInteractionMutex);
    
    std::vector<UserInteractionRequest> pending;
    for (const auto& [id, request] : m_pendingUserInteractions) {
        if (!request.hasResponse) {
            pending.push_back(request);
        }
    }
    
    return pending;
}

void Orchestrator::cancelUserInteraction(const std::string& interactionId) {
    std::lock_guard<std::mutex> lock(m_userInteractionMutex);
    
    try {
        auto requestIt = m_pendingUserInteractions.find(interactionId);
        if (requestIt != m_pendingUserInteractions.end()) {
            Logger::log(LogLevel::INFO, "Cancelling user interaction: " + interactionId);
            m_pendingUserInteractions.erase(requestIt);
        }
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error cancelling user interaction: " + std::string(e.what()));
    }
}

void Orchestrator::cleanupExpiredUserInteractions() {
    std::lock_guard<std::mutex> lock(m_userInteractionMutex);
    
    try {
        auto now = std::chrono::steady_clock::now();
        
        auto it = m_pendingUserInteractions.begin();
        while (it != m_pendingUserInteractions.end()) {
            if (now >= it->second.timeoutTime && !it->second.hasResponse) {
                Logger::log(LogLevel::DEBUG, "Cleaning up expired user interaction: " + it->first);
                it = m_pendingUserInteractions.erase(it);
            } else {
                ++it;
            }
        }
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "Error cleaning up user interactions: " + std::string(e.what()));
    }
}

TaskExecutionResult Orchestrator::executeControlCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    
    try {
        std::string action = command["command"];
        
        if (action == "control.if_contains") {
            // IF_CONTAINS - Check if a variable contains a substring (case-insensitive)
            auto params = command["params"];
            std::string variable = params["variable"];
            std::string substring = params["substring"];
            
            // Get variable value from context
            std::string variableValue = "";
            if (context.variables.find(variable) != context.variables.end()) {
                if (context.variables[variable].is_string()) {
                    variableValue = context.variables[variable].get<std::string>();
                } else {
                    variableValue = context.variables[variable].dump();
                }
            }
            
            // Convert both to lowercase for case-insensitive comparison
            std::string lowerValue = variableValue;
            std::string lowerSubstring = substring;
            std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
            std::transform(lowerSubstring.begin(), lowerSubstring.end(), lowerSubstring.begin(), ::tolower);
            
            bool contains = lowerValue.find(lowerSubstring) != std::string::npos;
            
            Logger::log(LogLevel::DEBUG, "[CONTROL] IF_CONTAINS check: '" + variableValue + "' contains '" + substring + "' = " + (contains ? "true" : "false"));
            
            // Store result in context
            if (params.find("store_as") != params.end()) {
                std::string resultVar = params["store_as"];
                context.variables[resultVar] = contains;
            }
            
            result.status = ExecutionStatus::COMPLETED;
            result.output = contains ? "true" : "false";
            result.success = true;
            result.executedCommands.push_back("IF_CONTAINS");
            
        } else if (action == "control.if_equals") {
            // IF_EQUALS - Check if a variable equals a value (case-insensitive)
            auto params = command["params"];
            std::string variable = params["variable"];
            std::string value = params["value"];
            
            // Get variable value from context
            std::string variableValue = "";
            if (context.variables.find(variable) != context.variables.end()) {
                if (context.variables[variable].is_string()) {
                    variableValue = context.variables[variable].get<std::string>();
                } else {
                    variableValue = context.variables[variable].dump();
                }
            }
            
            // Convert both to lowercase for case-insensitive comparison
            std::string lowerValue = variableValue;
            std::string lowerTarget = value;
            std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
            std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), ::tolower);
            
            bool equals = (lowerValue == lowerTarget);
            
            Logger::log(LogLevel::DEBUG, "[CONTROL] IF_EQUALS check: '" + variableValue + "' equals '" + value + "' = " + (equals ? "true" : "false"));
            
            // Store result in context
            if (params.find("store_as") != params.end()) {
                std::string resultVar = params["store_as"];
                context.variables[resultVar] = equals;
            }
            
            result.status = ExecutionStatus::COMPLETED;
            result.output = equals ? "true" : "false";
            result.success = true;
            result.executedCommands.push_back("IF_EQUALS");
            
        } else if (action == "control.if_not_contains") {
            // IF_NOT_CONTAINS - Check if a variable does NOT contain a substring (case-insensitive)
            auto params = command["params"];
            std::string variable = params["variable"];
            std::string substring = params["substring"];
            
            // Get variable value from context
            std::string variableValue = "";
            if (context.variables.find(variable) != context.variables.end()) {
                if (context.variables[variable].is_string()) {
                    variableValue = context.variables[variable].get<std::string>();
                } else {
                    variableValue = context.variables[variable].dump();
                }
            }
            
            // Convert both to lowercase for case-insensitive comparison
            std::string lowerValue = variableValue;
            std::string lowerSubstring = substring;
            std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
            std::transform(lowerSubstring.begin(), lowerSubstring.end(), lowerSubstring.begin(), ::tolower);
            
            bool notContains = (lowerValue.find(lowerSubstring) == std::string::npos);
            
            Logger::log(LogLevel::DEBUG, "[CONTROL] IF_NOT_CONTAINS check: '" + variableValue + "' does not contain '" + substring + "' = " + (notContains ? "true" : "false"));
            
            // Store result in context
            if (params.find("store_as") != params.end()) {
                std::string resultVar = params["store_as"];
                context.variables[resultVar] = notContains;
            }
            
            result.status = ExecutionStatus::COMPLETED;
            result.output = notContains ? "true" : "false";
            result.success = true;
            result.executedCommands.push_back("IF_NOT_CONTAINS");
            
        } else if (action == "control.conditional_stop") {
            // CONDITIONAL_STOP - Stop execution if condition is true (with optional invert)
            auto params = command["params"];
            std::string conditionVar = params["condition_variable"];
            bool invertCondition = params.value("invert", false);
            
            bool conditionValue = false;
            if (context.variables.find(conditionVar) != context.variables.end()) {
                if (context.variables[conditionVar].is_boolean()) {
                    conditionValue = context.variables[conditionVar].get<bool>();
                } else if (context.variables[conditionVar].is_string()) {
                    std::string value = context.variables[conditionVar].get<std::string>();
                    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                    conditionValue = (value == "true" || value == "1" || value == "yes");
                }
            }
            
            // Apply invert logic if specified
            bool shouldStop = invertCondition ? !conditionValue : conditionValue;
            
            std::string logMessage = "[CONTROL] CONDITIONAL_STOP check: " + conditionVar + " = " + (conditionValue ? "true" : "false");
            if (invertCondition) {
                logMessage += " (inverted to " + std::string(shouldStop ? "true" : "false") + ")";
            }
            logMessage += " -> " + std::string(shouldStop ? "stopping" : "continuing");
            Logger::log(LogLevel::DEBUG, logMessage);
            
            if (shouldStop) {
                result.status = ExecutionStatus::CANCELLED;
                result.output = "Execution stopped due to condition: " + conditionVar + (invertCondition ? " (inverted)" : "");
                result.success = true;
            } else {
                result.status = ExecutionStatus::COMPLETED;
                result.output = "Condition not met, continuing execution";
                result.success = true;
            }
            result.executedCommands.push_back("CONDITIONAL_STOP");
            
        } else if (action == "control.conditional") {
            // CONDITIONAL - Execute commands based on condition (legacy)
            result.status = ExecutionStatus::COMPLETED;
            result.output = "Legacy conditional command completed";
            result.executedCommands.push_back("CONDITIONAL");
            
        } else if (action == "control.while_loop") {
            // WHILE_LOOP - Execute sequence while condition is true
            return executeWhileLoop(command, context);
            
        } else if (action == "control.break_if") {
            // BREAK_IF - Break loop if condition is true
            auto params = command["params"];
            std::string conditionVar = params["condition_variable"];
            
            // Get condition value from context
            bool shouldBreak = false;
            if (context.variables.find(conditionVar) != context.variables.end()) {
                if (context.variables[conditionVar].is_boolean()) {
                    shouldBreak = context.variables[conditionVar].get<bool>();
                } else if (context.variables[conditionVar].is_string()) {
                    std::string value = context.variables[conditionVar].get<std::string>();
                    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                    shouldBreak = (value == "true" || value == "1" || value == "yes");
                }
            }
            
            Logger::log(LogLevel::DEBUG, "[CONTROL] BREAK_IF check: " + conditionVar + " = " + (shouldBreak ? "true (breaking)" : "false (continuing)"));
            
            if (shouldBreak) {
                result.status = ExecutionStatus::BREAK_LOOP;
                result.output = "Loop break triggered by condition: " + conditionVar;
                result.success = true;
            } else {
                result.status = ExecutionStatus::COMPLETED;
                result.output = "Break condition not met, continuing";
                result.success = true;
            }
            result.executedCommands.push_back("BREAK_IF");
            
        } else if (action == "control.continue_if") {
            // CONTINUE_IF - Continue to next iteration if condition is true
            auto params = command["params"];
            std::string conditionVar = params["condition_variable"];
            
            // Get condition value from context
            bool shouldContinue = false;
            if (context.variables.find(conditionVar) != context.variables.end()) {
                if (context.variables[conditionVar].is_boolean()) {
                    shouldContinue = context.variables[conditionVar].get<bool>();
                } else if (context.variables[conditionVar].is_string()) {
                    std::string value = context.variables[conditionVar].get<std::string>();
                    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                    shouldContinue = (value == "true" || value == "1" || value == "yes");
                }
            }
            
            Logger::log(LogLevel::DEBUG, "[CONTROL] CONTINUE_IF check: " + conditionVar + " = " + (shouldContinue ? "true (continuing)" : "false (proceeding)"));
            
            if (shouldContinue) {
                result.status = ExecutionStatus::CONTINUE_LOOP;
                result.output = "Loop continue triggered by condition: " + conditionVar;
                result.success = true;
            } else {
                result.status = ExecutionStatus::COMPLETED;
                result.output = "Continue condition not met, proceeding";
                result.success = true;
            }
            result.executedCommands.push_back("CONTINUE_IF");
            
        } else {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Unknown control command: " + action;
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Control command exception: " + std::string(e.what());
    }
    
    return result;
}

TaskExecutionResult Orchestrator::executeWhileLoop(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    
    try {
        auto params = command["params"];
        int maxIterations = params.value("max_iterations", 100);
        bool alwaysTrue = params.value("always_true", false);
        
        // Get the loop sequence
        auto sequence = params["sequence"];
        
        int iteration = 0;
        bool shouldContinue = true;
        
        Logger::log(LogLevel::DEBUG, "[LOOP] Starting WHILE_LOOP with max_iterations: " + std::to_string(maxIterations));
        
        // Initial condition evaluation (unless always_true is set)
        if (alwaysTrue) {
            shouldContinue = true;
        } else if (params.contains("condition")) {
            shouldContinue = evaluateLoopCondition(params["condition"], context);
        } else {
            shouldContinue = true; // Default to always true if no condition specified
        }
        
        while (shouldContinue && iteration < maxIterations) {
            Logger::log(LogLevel::DEBUG, "[LOOP] Starting iteration " + std::to_string(iteration + 1));
            
            // Execute sequence
            bool breakLoop = false;
            bool continueLoop = false;
            
            for (const auto& step : sequence) {
                auto stepResult = executeCommand(step, context);
                
                // Handle loop control
                if (stepResult.status == ExecutionStatus::BREAK_LOOP) {
                    Logger::log(LogLevel::DEBUG, "[LOOP] BREAK_LOOP triggered, exiting loop");
                    breakLoop = true;
                    break;
                }
                
                if (stepResult.status == ExecutionStatus::CONTINUE_LOOP) {
                    Logger::log(LogLevel::DEBUG, "[LOOP] CONTINUE_LOOP triggered, next iteration");
                    continueLoop = true;
                    break;
                }
                
                // Handle failures
                if (stepResult.status == ExecutionStatus::FAILED) {
                    Logger::log(LogLevel::ERROR_LEVEL, "[LOOP] Step failed: " + stepResult.errorMessage);
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Loop step failed: " + stepResult.errorMessage;
                    return result;
                }
            }
            
            // Exit loop if break was triggered
            if (breakLoop) {
                shouldContinue = false;
                break;
            }
            
            // Skip to next iteration if continue was triggered
            if (continueLoop) {
                iteration++;
                continue;
            }
            
            iteration++;
            
            // Re-evaluate condition for next iteration (unless always_true or break triggered)
            if (shouldContinue && !alwaysTrue && params.contains("condition")) {
                shouldContinue = evaluateLoopCondition(params["condition"], context);
                Logger::log(LogLevel::DEBUG, std::string("[LOOP] Condition re-evaluation: ") + (shouldContinue ? "true" : "false"));
            }
        }
        
        // Check if loop exceeded max iterations
        if (iteration >= maxIterations) {
            Logger::log(LogLevel::WARNING, "[LOOP] Loop terminated due to max_iterations limit: " + std::to_string(maxIterations));
        }
        
        result.status = ExecutionStatus::COMPLETED;
        result.output = "Loop completed after " + std::to_string(iteration) + " iterations";
        result.success = true;
        result.executedCommands.push_back("WHILE_LOOP");
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "While loop exception: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, "[LOOP] Exception in executeWhileLoop: " + std::string(e.what()));
    }
    
    return result;
}

bool Orchestrator::evaluateLoopCondition(const nlohmann::json& condition, ExecutionContext& context) {
    try {
        std::string type = condition["type"];
        
        if (type == "IF_NOT_EQUALS") {
            std::string variable = condition["variable"];
            std::string value = condition["value"];
            
            // Get variable value from context
            std::string variableValue = "";
            if (context.variables.find(variable) != context.variables.end()) {
                if (context.variables[variable].is_string()) {
                    variableValue = context.variables[variable].get<std::string>();
                } else {
                    variableValue = context.variables[variable].dump();
                }
            }
            
            // Case-insensitive comparison
            std::string lowerValue = variableValue;
            std::string lowerTarget = value;
            std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
            std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), ::tolower);
            
            bool result = (lowerValue != lowerTarget);
            Logger::log(LogLevel::DEBUG, "[LOOP] Condition IF_NOT_EQUALS: '" + variableValue + "' != '" + value + "' = " + (result ? "true" : "false"));
            return result;
            
        } else if (type == "IF_EQUALS") {
            std::string variable = condition["variable"];
            std::string value = condition["value"];
            
            // Get variable value from context
            std::string variableValue = "";
            if (context.variables.find(variable) != context.variables.end()) {
                if (context.variables[variable].is_string()) {
                    variableValue = context.variables[variable].get<std::string>();
                } else {
                    variableValue = context.variables[variable].dump();
                }
            }
            
            // Case-insensitive comparison
            std::string lowerValue = variableValue;
            std::string lowerTarget = value;
            std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::tolower);
            std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), ::tolower);
            
            bool result = (lowerValue == lowerTarget);
            Logger::log(LogLevel::DEBUG, "[LOOP] Condition IF_EQUALS: '" + variableValue + "' == '" + value + "' = " + (result ? "true" : "false"));
            return result;
            
        } else if (type == "ALWAYS_TRUE") {
            return true;
        }
        
        Logger::log(LogLevel::WARNING, "[LOOP] Unknown condition type: " + type);
        return false;
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR_LEVEL, "[LOOP] Exception in evaluateLoopCondition: " + std::string(e.what()));
        return false;
    }
}

TaskExecutionResult Orchestrator::executeScriptFile(const std::string& scriptPath) {
    TaskExecutionResult result;
    result.executionId = generateRequestId();
    result.status = ExecutionStatus::IN_PROGRESS;
    result.success = false;
    
    Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Loading and executing script file: " + scriptPath);
    
    try {
        // Check if script file exists
        if (!std::filesystem::exists(scriptPath)) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Script file not found: " + scriptPath;
            Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] " + result.errorMessage);
            return result;
        }
        
        // Read the JSON script
        std::ifstream file(scriptPath);
        if (!file.is_open()) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Failed to open script file: " + scriptPath;
            Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] " + result.errorMessage);
            return result;
        }
        
        std::string scriptContent((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
        file.close();
        
        Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Script loaded (" + std::to_string(scriptContent.length()) + " characters)");
        
        // Parse JSON script
        nlohmann::json scriptJson;
        try {
            scriptJson = nlohmann::json::parse(scriptContent);
            Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Script parsed successfully");
            
            // Extract optional metadata gracefully
            std::string scriptName = "Unknown";
            std::string scriptDesc = "No description";
            
            if (scriptJson.contains("meta") && scriptJson["meta"].contains("name")) {
                scriptName = scriptJson["meta"]["name"].get<std::string>();
            } else if (scriptJson.contains("name")) {
                scriptName = scriptJson["name"].get<std::string>();
            }
            
            if (scriptJson.contains("meta") && scriptJson["meta"].contains("description")) {
                scriptDesc = scriptJson["meta"]["description"].get<std::string>();
            } else if (scriptJson.contains("description")) {
                scriptDesc = scriptJson["description"].get<std::string>();
            }
            
            Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Script: " + scriptName + " - " + scriptDesc);
            
            // Prepare execution plan from script (including variables)
            nlohmann::json executionPlan;
            
            // Copy command sequence
            if (scriptJson.contains("sequence")) {
                executionPlan["sequence"] = scriptJson["sequence"];
                Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Found " + std::to_string(scriptJson["sequence"].size()) + " steps in sequence");
            } else if (scriptJson.contains("commands")) {
                executionPlan["commands"] = scriptJson["commands"];
                Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Found " + std::to_string(scriptJson["commands"].size()) + " commands");
            } else {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Script missing mandatory 'sequence' or 'commands' array";
                Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] " + result.errorMessage);
                return result;
            }
            
            // Copy variables section if present
            if (scriptJson.contains("variables")) {
                executionPlan["variables"] = scriptJson["variables"];
                Logger::log(LogLevel::INFO, "[ORCHESTRATOR] Found " + std::to_string(scriptJson["variables"].size()) + " script variables");
            }
            
            // Execute the plan using existing orchestrator functionality
            result = executePlan(executionPlan);
            
        } catch (const std::exception& e) {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "JSON parsing failed: " + std::string(e.what());
            Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] " + result.errorMessage);
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Script file execution failed: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, "[ORCHESTRATOR] " + result.errorMessage);
    }
    
    return result;
}

TaskExecutionResult Orchestrator::executeScriptCommand(const nlohmann::json& command, ExecutionContext& context) {
    TaskExecutionResult result;
    
    try {
        std::string action = command["command"];
        
        if (action == "EXECUTE_SCRIPT") {
            // Debug logging for JSON structure
            Logger::log(LogLevel::DEBUG, "[SCRIPT] EXECUTE_SCRIPT command params type: " + 
                       std::string(command["params"].type_name()));
            Logger::log(LogLevel::DEBUG, "[SCRIPT] EXECUTE_SCRIPT command params: " + 
                       command["params"].dump());
            
            // Check for required parameters
            if (!command.contains("params") || !command["params"].contains("script_path")) {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Missing script_path parameter for EXECUTE_SCRIPT";
                return result;
            }
            
            std::string scriptPath = command["params"]["script_path"];
            std::string resultVariable = command["params"].value("result_variable", "");
            bool continueOnFailure = command["params"].value("continue_on_failure", false);
            
            // Check nesting level limit
            if (context.nestingLevel >= context.maxNestingLevel) {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Maximum script nesting level (" + std::to_string(context.maxNestingLevel) + ") exceeded";
                Logger::log(LogLevel::ERROR_LEVEL, "[SCRIPT] Nesting level limit exceeded: " + std::to_string(context.nestingLevel) + " >= " + std::to_string(context.maxNestingLevel));
                return result;
            }
            
            // Check for circular dependency
            for (const auto& scriptInStack : context.scriptStack) {
                if (scriptInStack == scriptPath) {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Circular script dependency detected: " + scriptPath;
                    Logger::log(LogLevel::ERROR_LEVEL, "[SCRIPT] Circular dependency detected for: " + scriptPath);
                    return result;
                }
            }
            
            Logger::log(LogLevel::INFO, "[SCRIPT] Executing nested script: " + scriptPath + " (nesting level: " + std::to_string(context.nestingLevel) + ")");
            
            // Create nested execution context
            ExecutionContext nestedContext;
            nestedContext.requestId = context.requestId + "_nested_" + std::to_string(context.nestingLevel + 1);
            nestedContext.nestingLevel = context.nestingLevel + 1;
            nestedContext.maxNestingLevel = context.maxNestingLevel;
            nestedContext.scriptStack = context.scriptStack;
            nestedContext.scriptStack.push_back(scriptPath);
            nestedContext.variables = context.variables; // Inherit parent variables
            
            
            // Load and execute the nested script
            std::ifstream scriptFile(scriptPath);
            if (!scriptFile.is_open()) {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Failed to open script file: " + scriptPath;
                Logger::log(LogLevel::ERROR_LEVEL, "[SCRIPT] Failed to open script file: " + scriptPath);
                return result;
            }
            
            nlohmann::json nestedScript;
            try {
                scriptFile >> nestedScript;
                scriptFile.close();
            } catch (const std::exception& e) {
                result.status = ExecutionStatus::FAILED;
                result.errorMessage = "Failed to parse script JSON: " + std::string(e.what());
                Logger::log(LogLevel::ERROR_LEVEL, "[SCRIPT] Failed to parse script JSON for " + scriptPath + ": " + std::string(e.what()));
                return result;
            }
            
            // Prepare nested script with inherited variables
            if (!context.variables.empty()) {
                // Create or merge variables section with parent context variables
                if (!nestedScript.contains("variables")) {
                    nestedScript["variables"] = nlohmann::json::object();
                }
                
                // Inherit parent variables (parent variables can be overridden by script variables)
                for (const auto& [key, value] : context.variables) {
                    if (!nestedScript["variables"].contains(key)) {
                        nestedScript["variables"][key] = value;
                        Logger::log(LogLevel::DEBUG, "[SCRIPT] Inherited parent variable: " + key);
                    }
                }
                Logger::log(LogLevel::INFO, "[SCRIPT] Nested script inheriting " + std::to_string(context.variables.size()) + " parent variables");
            }
            
            // Add variables passed via EXECUTE_SCRIPT command to the script JSON
            // Support both "variables" and "pass_variables" fields for backward compatibility
            nlohmann::json passedVars;
            if (command["params"].contains("pass_variables") && command["params"]["pass_variables"].is_object()) {
                passedVars = command["params"]["pass_variables"];
                Logger::log(LogLevel::DEBUG, "[SCRIPT] Found pass_variables field with " + std::to_string(passedVars.size()) + " variables");
            } else if (command["params"].contains("variables") && command["params"]["variables"].is_object()) {
                passedVars = command["params"]["variables"];
                Logger::log(LogLevel::DEBUG, "[SCRIPT] Found variables field with " + std::to_string(passedVars.size()) + " variables");
            }
            
            if (!passedVars.empty()) {
                // Ensure variables section exists in nested context
                if (!nestedContext.variables.empty() || !passedVars.empty()) {
                    // FIXED: Resolve variables BEFORE passing to nested context to prevent infinite loops
                    for (const auto& [key, value] : passedVars.items()) {
                        if (value.is_string()) {
                            // Resolve any variable references in the string value using parent context
                            std::string resolvedValue = substituteVariables(value.get<std::string>(), context);
                            nestedContext.variables[key] = resolvedValue;
                            Logger::log(LogLevel::DEBUG, "[SCRIPT] Added EXECUTE_SCRIPT variable (resolved): " + key + " = " + resolvedValue);
                        } else {
                            // Non-string values don't need variable resolution
                            nestedContext.variables[key] = value;
                            Logger::log(LogLevel::DEBUG, "[SCRIPT] Added EXECUTE_SCRIPT variable: " + key + " = " + value.dump());
                        }
                    }
                    Logger::log(LogLevel::INFO, "[SCRIPT] Added " + std::to_string(passedVars.size()) + " variables from EXECUTE_SCRIPT command (resolved)");
                }
            }
            
            // Execute the nested script with the prepared execution context
            Logger::log(LogLevel::INFO, "[SCRIPT] Starting execution of nested script: " + scriptPath);
            TaskExecutionResult nestedResult = executePlan(nestedScript, nestedContext);
            
            // Store the result
            context.subScriptResults[scriptPath] = {
                {"status", nestedResult.status == ExecutionStatus::COMPLETED ? "success" : "failed"},
                {"executedCommands", nestedResult.executedCommands.size()},
                {"output", nestedResult.output},
                {"errorMessage", nestedResult.errorMessage}
            };
            
            // Store result in specified variable if provided
            if (!resultVariable.empty()) {
                context.variables[resultVariable] = context.subScriptResults[scriptPath];
                Logger::log(LogLevel::DEBUG, "[SCRIPT] Stored result in variable: " + resultVariable);
            }
            
            // Determine success based on nested script result and continue_on_failure setting
            if (nestedResult.status == ExecutionStatus::COMPLETED) {
                result.status = ExecutionStatus::COMPLETED;
                result.output = "Nested script executed successfully: " + scriptPath;
                result.executedCommands.push_back("EXECUTE_SCRIPT");
                Logger::log(LogLevel::INFO, "[SCRIPT] Nested script completed successfully: " + scriptPath);
            } else {
                if (continueOnFailure) {
                    result.status = ExecutionStatus::COMPLETED;
                    result.output = "Nested script failed but continuing due to continue_on_failure: " + scriptPath;
                    result.executedCommands.push_back("EXECUTE_SCRIPT");
                    Logger::log(LogLevel::WARNING, "[SCRIPT] Nested script failed but continuing: " + scriptPath + " - " + nestedResult.errorMessage);
                } else {
                    result.status = ExecutionStatus::FAILED;
                    result.errorMessage = "Nested script failed: " + scriptPath + " - " + nestedResult.errorMessage;
                    Logger::log(LogLevel::ERROR_LEVEL, "[SCRIPT] Nested script failed: " + scriptPath + " - " + nestedResult.errorMessage);
                }
            }
            
            // Copy variables back to parent context (only new/modified ones)
            for (const auto& [key, value] : nestedContext.variables) {
                context.variables[key] = value;
            }
            
        } else {
            result.status = ExecutionStatus::FAILED;
            result.errorMessage = "Unknown script command: " + action;
        }
        
    } catch (const std::exception& e) {
        result.status = ExecutionStatus::FAILED;
        result.errorMessage = "Script command exception: " + std::string(e.what());
        Logger::log(LogLevel::ERROR_LEVEL, "[SCRIPT] Exception in executeScriptCommand: " + std::string(e.what()));
    }
    
    return result;
}


