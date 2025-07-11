#include "orchestrator_facade.h"
#include "execution_engine.h"
#include "state_manager.h"
#include "event_manager.h"
#include "script_manager.h"
#include "feedback_controller.h"
#include "conversation_manager.h"
#include "orchestrator.h"  // For ExecutionContext
#include "../command_parser/command_parser.h"
#include "../llm_connector/llm_connector.h"
#include "../task_engine/task_engine.h"
#include "../common/structured_logger.h"
#include "../common/config_manager.h"
#include <thread>
#include <queue>

namespace burwell {

OrchestratorFacade::OrchestratorFacade()
    : m_isRunning(false)
    , m_isPaused(false)
    , m_emergencyStop(false)
    , m_autoMode(false)
    , m_confirmationRequired(true)
    , m_maxConcurrentTasks(5)
    , m_executionTimeoutMs(30000)
    , m_mainLoopDelayMs(100)
    , m_commandSequenceDelayMs(1000)
    , m_errorRecoveryDelayMs(2000)
    , m_errorRecoveryEnabled(true)
    , m_maxErrorRetries(3)
    , m_feedbackLoopEnabled(true) {
    
    // Create internal components
    m_executionEngine = std::make_unique<ExecutionEngine>();
    m_stateManager = std::make_unique<StateManager>();
    m_eventManager = std::make_unique<EventManager>();
    m_scriptManager = std::make_unique<ScriptManager>();
    m_feedbackController = std::make_unique<FeedbackController>();
    m_conversationManager = std::make_unique<ConversationManager>();
    
    SLOG_INFO().message("OrchestratorFacade initialized");
}

OrchestratorFacade::~OrchestratorFacade() {
    shutdown();
    SLOG_INFO().message("OrchestratorFacade destroyed");
}

void OrchestratorFacade::initialize() {
    SLOG_INFO().message("Initializing OrchestratorFacade");
    
    // Initialize components
    initializeComponents();
    
    // Connect components
    connectComponents();
    
    // Load configuration
    auto& config = ConfigManager::getInstance();
    
    // Apply orchestrator settings
    try {
        m_maxConcurrentTasks = config.get<int>("orchestrator.max_concurrent_tasks");
    } catch (const std::runtime_error&) {
        // Use default value
    }
    
    try {
        m_executionTimeoutMs = config.get<int>("orchestrator.execution_timeout_ms");
    } catch (const std::runtime_error&) {
        // Use default value
    }
    
    try {
        m_commandSequenceDelayMs = config.get<int>("orchestrator.command_sequence_delay_ms");
    } catch (const std::runtime_error&) {
        // Use default value
    }
    
    try {
        m_scriptManager->setMaxNestingLevel(config.get<int>("orchestrator.max_script_nesting_level"));
    } catch (const std::runtime_error&) {
        // Use default value
    }
    
    // Enable event history
    m_eventManager->enableEventHistory(true);
    
    // Raise initialization event
    m_eventManager->raiseEvent(OrchestratorEvent::EXECUTION_STARTED, "OrchestratorFacade initialized");
    
    SLOG_INFO().message("OrchestratorFacade initialization complete");
}

void OrchestratorFacade::run() {
    if (m_isRunning) {
        SLOG_WARNING().message("OrchestratorFacade is already running");
        return;
    }
    
    m_isRunning = true;
    m_emergencyStop = false;
    
    // Start worker thread
    m_workerThread = std::thread(&OrchestratorFacade::workerThreadFunction, this);
    
    // Start feedback loop if enabled
    if (m_feedbackLoopEnabled) {
        m_feedbackController->startContinuousMonitoring();
    }
    
    SLOG_INFO().message("OrchestratorFacade started");
}

void OrchestratorFacade::shutdown() {
    if (!m_isRunning) {
        return;
    }
    
    SLOG_INFO().message("Shutting down OrchestratorFacade");
    
    // Stop monitoring
    m_feedbackController->stopContinuousMonitoring();
    
    // Signal shutdown
    m_isRunning = false;
    m_queueCondition.notify_all();
    
    // Wait for worker thread
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    
    // Clean up conversations
    m_conversationManager->cleanupExpiredConversations();
    
    // Raise shutdown event
    m_eventManager->raiseEvent(OrchestratorEvent::EXECUTION_PAUSED, "OrchestratorFacade shutdown");
    
    SLOG_INFO().message("OrchestratorFacade shutdown complete");
}

TaskExecutionResult OrchestratorFacade::processUserRequest(const std::string& userInput) {
    // Create request and process synchronously
    std::string requestId = m_stateManager->createRequest(userInput);
    
    // Log activity
    logActivity("Processing user request: " + userInput);
    
    // Raise event
    m_eventManager->raiseEvent(OrchestratorEvent::USER_REQUEST, userInput, requestId);
    
    // Process request
    TaskExecutionResult result = processRequestInternal(requestId, userInput);
    
    // Store result
    m_stateManager->markExecutionComplete(requestId, result);
    
    // Raise completion event
    if (result.success) {
        m_eventManager->raiseEvent(OrchestratorEvent::TASK_COMPLETED, "Request completed", requestId);
    } else {
        m_eventManager->raiseEvent(OrchestratorEvent::TASK_FAILED, result.errorMessage, requestId);
    }
    
    return result;
}

TaskExecutionResult OrchestratorFacade::executeTask(const std::string& taskId) {
    TaskExecutionResult result;
    
    if (!m_taskEngine) {
        result.success = false;
        result.errorMessage = "Task engine not initialized";
        return result;
    }
    
    // Create execution context
    std::string requestId = m_stateManager->createRequest("Execute task: " + taskId);
    // ExecutionContext& context = m_stateManager->getExecutionContext(requestId); // Unused variable
    
    // Execute task
    result = m_taskEngine->executeTask(taskId);
    
    // Update success metrics
    m_feedbackController->updateCommandSuccessRate("TASK_" + taskId, result.success);
    
    return result;
}

TaskExecutionResult OrchestratorFacade::executePlan(const nlohmann::json& executionPlan) {
    // Create execution context
    std::string requestId = m_stateManager->createRequest("Execute plan");
    ExecutionContext& context = m_stateManager->getExecutionContext(requestId);
    
    // Validate plan - check for both 'commands' and 'sequence' arrays
    nlohmann::json commandArray;
    if (executionPlan.contains("commands") && executionPlan["commands"].is_array()) {
        commandArray = executionPlan["commands"];
    } else if (executionPlan.contains("sequence") && executionPlan["sequence"].is_array()) {
        commandArray = executionPlan["sequence"];
    } else {
        TaskExecutionResult result;
        result.success = false;
        result.errorMessage = "Invalid execution plan: missing 'commands' or 'sequence' array";
        return result;
    }
    
    // Execute with error recovery if enabled
    if (m_errorRecoveryEnabled) {
        // Create a normalized plan with 'commands' for error recovery
        nlohmann::json normalizedPlan = executionPlan;
        normalizedPlan["commands"] = commandArray;
        return executeWithErrorRecovery(normalizedPlan, requestId);
    } else {
        return m_executionEngine->executeCommandSequence(commandArray, context);
    }
}

TaskExecutionResult OrchestratorFacade::executeScriptFile(const std::string& scriptPath) {
    // Create execution context
    std::string requestId = m_stateManager->createRequest("Execute script: " + scriptPath);
    ExecutionContext& context = m_stateManager->getExecutionContext(requestId);
    
    // Execute script
    return m_scriptManager->executeScriptFile(scriptPath, context);
}

std::string OrchestratorFacade::processUserRequestAsync(const std::string& userInput) {
    std::string requestId = m_stateManager->createRequest(userInput);
    
    // Add to queue
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_requestQueue.push(requestId);
    }
    
    // Notify worker thread
    m_queueCondition.notify_one();
    
    // Log activity
    logActivity("Queued async request: " + requestId);
    
    return requestId;
}

TaskExecutionResult OrchestratorFacade::getExecutionResult(const std::string& requestId) {
    return m_stateManager->getExecutionResult(requestId);
}

std::vector<std::string> OrchestratorFacade::getActiveRequests() const {
    return m_stateManager->getActiveRequests();
}

void OrchestratorFacade::setCommandParser(std::shared_ptr<CommandParser> parser) {
    m_commandParser = parser;
}

void OrchestratorFacade::setLLMConnector(std::shared_ptr<LLMConnector> connector) {
    m_llmConnector = connector;
    m_conversationManager->setLLMConnector(connector);
    m_feedbackController->setLLMConnector(connector);
}

void OrchestratorFacade::setTaskEngine(std::shared_ptr<TaskEngine> engine) {
    m_taskEngine = engine;
}

void OrchestratorFacade::setOCAL(std::shared_ptr<OCAL> ocal) {
    m_ocal = ocal;
    m_executionEngine->setOCAL(ocal);
}

void OrchestratorFacade::setEnvironmentalPerception(std::shared_ptr<EnvironmentalPerception> perception) {
    m_perception = perception;
    m_executionEngine->setEnvironmentalPerception(perception);
    m_feedbackController->setEnvironmentalPerception(perception);
    m_conversationManager->setEnvironmentalPerception(perception);
}

void OrchestratorFacade::setUIModule(std::shared_ptr<UIModule> ui) {
    m_ui = ui;
    m_executionEngine->setUIModule(ui);
    m_conversationManager->setUIModule(ui);
}

void OrchestratorFacade::setAutoMode(bool enabled) {
    m_autoMode = enabled;
}

void OrchestratorFacade::setConfirmationRequired(bool required) {
    m_confirmationRequired = required;
    m_executionEngine->setConfirmationRequired(required);
}

void OrchestratorFacade::setMaxConcurrentTasks(int maxTasks) {
    m_maxConcurrentTasks = maxTasks;
}

void OrchestratorFacade::setExecutionTimeout(int timeoutMs) {
    m_executionTimeoutMs = timeoutMs;
    m_executionEngine->setExecutionTimeoutMs(timeoutMs);
}

void OrchestratorFacade::setMainLoopDelayMs(int delayMs) {
    m_mainLoopDelayMs = delayMs;
}

void OrchestratorFacade::setCommandSequenceDelayMs(int delayMs) {
    m_commandSequenceDelayMs = delayMs;
    m_executionEngine->setCommandSequenceDelayMs(delayMs);
}

void OrchestratorFacade::setErrorRecoveryDelayMs(int delayMs) {
    m_errorRecoveryDelayMs = delayMs;
}

void OrchestratorFacade::pauseExecution() {
    m_isPaused = true;
    m_eventManager->raiseEvent(OrchestratorEvent::EXECUTION_PAUSED, "Execution paused");
    SLOG_INFO().message("Execution paused");
}

void OrchestratorFacade::resumeExecution() {
    m_isPaused = false;
    m_queueCondition.notify_all();
    m_eventManager->raiseEvent(OrchestratorEvent::EXECUTION_RESUMED, "Execution resumed");
    SLOG_INFO().message("Execution resumed");
}

void OrchestratorFacade::cancelExecution(const std::string& requestId) {
    // Remove from queue if present
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        std::queue<std::string> newQueue;
        while (!m_requestQueue.empty()) {
            std::string id = m_requestQueue.front();
            m_requestQueue.pop();
            if (id != requestId) {
                newQueue.push(id);
            }
        }
        m_requestQueue = newQueue;
    }
    
    // Mark as cancelled
    TaskExecutionResult result;
    result.success = false;
    result.status = ExecutionStatus::CANCELLED;
    result.errorMessage = "Execution cancelled by user";
    m_stateManager->markExecutionComplete(requestId, result);
    
    logActivity("Cancelled execution: " + requestId);
}

void OrchestratorFacade::emergencyStop() {
    m_emergencyStop = true;
    m_isPaused = true;
    
    // Clear request queue
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        std::queue<std::string> empty;
        std::swap(m_requestQueue, empty);
    }
    
    m_eventManager->raiseEvent(OrchestratorEvent::EMERGENCY_STOP, "Emergency stop activated");
    SLOG_CRITICAL().message("Emergency stop activated");
}

void OrchestratorFacade::addEventListener(std::function<void(const EventData&)> listener) {
    m_eventManager->addEventListener(listener);
}

void OrchestratorFacade::raiseEvent(const EventData& event) {
    m_eventManager->raiseEvent(event);
}

bool OrchestratorFacade::isRunning() const {
    return m_isRunning && !m_emergencyStop;
}

bool OrchestratorFacade::isIdle() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_requestQueue.empty() && m_stateManager->getActiveExecutionCount() == 0;
}

nlohmann::json OrchestratorFacade::getSystemStatus() const {
    nlohmann::json status = {
        {"isRunning", m_isRunning.load()},
        {"isPaused", m_isPaused.load()},
        {"emergencyStop", m_emergencyStop.load()},
        {"autoMode", m_autoMode},
        {"confirmationRequired", m_confirmationRequired},
        {"activeRequests", m_stateManager->getActiveRequests()},
        {"queuedRequests", 0},
        {"activeConversations", m_conversationManager->getActiveConversationCount()},
        {"feedbackLoopActive", m_feedbackController->isMonitoringActive()},
        {"successMetrics", m_feedbackController->getSuccessMetrics()}
    };
    
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        status["queuedRequests"] = m_requestQueue.size();
    }
    
    return status;
}

std::vector<std::string> OrchestratorFacade::getRecentActivity() const {
    return m_stateManager->getRecentActivity();
}

void OrchestratorFacade::setErrorRecoveryEnabled(bool enabled) {
    m_errorRecoveryEnabled = enabled;
}

void OrchestratorFacade::setMaxErrorRetries(int maxRetries) {
    m_maxErrorRetries = maxRetries;
}

void OrchestratorFacade::setFeedbackLoopEnabled(bool enabled) {
    m_feedbackLoopEnabled = enabled;
    if (enabled && m_isRunning) {
        m_feedbackController->startContinuousMonitoring();
    } else {
        m_feedbackController->stopContinuousMonitoring();
    }
}

void OrchestratorFacade::setEnvironmentCheckInterval(int intervalMs) {
    m_feedbackController->setEnvironmentCheckIntervalMs(intervalMs);
}

// Private methods

void OrchestratorFacade::initializeComponents() {
    // Connect execution engine to script manager
    m_scriptManager->setExecutionEngine(std::shared_ptr<ExecutionEngine>(m_executionEngine.get(), [](ExecutionEngine*){}));
    
    // Configure components with settings
    m_executionEngine->setCommandSequenceDelayMs(m_commandSequenceDelayMs);
    m_executionEngine->setExecutionTimeoutMs(m_executionTimeoutMs);
    m_executionEngine->setConfirmationRequired(m_confirmationRequired);
    
    m_conversationManager->setMaxConversationTurns(10);
    m_feedbackController->setContinuousMonitoringEnabled(m_feedbackLoopEnabled);
}

void OrchestratorFacade::connectComponents() {
    // Components are connected through setters when external dependencies are provided
}

void OrchestratorFacade::workerThreadFunction() {
    SLOG_INFO().message("Worker thread started");
    
    while (m_isRunning) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        
        // Wait for requests or shutdown
        m_queueCondition.wait(lock, [this] {
            return !m_requestQueue.empty() || !m_isRunning || m_isPaused;
        });
        
        if (!m_isRunning) {
            break;
        }
        
        if (m_isPaused) {
            continue;
        }
        
        // Check concurrent task limit
        if (m_stateManager->getActiveExecutionCount() >= static_cast<size_t>(m_maxConcurrentTasks)) {
            // Wait a bit before checking again
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(m_mainLoopDelayMs));
            continue;
        }
        
        // Process next request
        if (!m_requestQueue.empty()) {
            std::string requestId = m_requestQueue.front();
            m_requestQueue.pop();
            lock.unlock();
            
            processRequestQueue();
        }
    }
    
    SLOG_INFO().message("Worker thread stopped");
}

void OrchestratorFacade::processRequestQueue() {
    // Get next request from state manager
    auto activeRequests = m_stateManager->getActiveRequests();
    
    for (const auto& requestId : activeRequests) {
        if (!m_stateManager->hasExecutionResult(requestId)) {
            // Get request details
            const ExecutionContext& context = m_stateManager->getExecutionContext(requestId);
            
            // Mark as active
            m_stateManager->markExecutionActive(requestId);
            
            // Process the request
            TaskExecutionResult result = processRequestInternal(requestId, context.originalRequest);
            
            // Store result
            m_stateManager->markExecutionComplete(requestId, result);
            
            // Raise completion event
            if (result.success) {
                m_eventManager->raiseEvent(OrchestratorEvent::TASK_COMPLETED, 
                                         "Request completed: " + requestId, requestId);
            } else {
                m_eventManager->raiseEvent(OrchestratorEvent::TASK_FAILED, 
                                         result.errorMessage, requestId);
            }
            
            break;  // Process one at a time
        }
    }
}

TaskExecutionResult OrchestratorFacade::processRequestInternal(const std::string& requestId, const std::string& userInput) {
    SLOG_INFO().message("Processing request").context("request_id", requestId).context("user_input", userInput);
    
    TaskExecutionResult result;
    result.executionId = requestId;
    
    try {
        // Parse user request
        result = parseUserRequest(userInput, requestId);
        if (!result.success) {
            return result;
        }
        
        // Generate execution plan
        result = generateExecutionPlan(userInput, requestId);
        if (!result.success) {
            return result;
        }
        
        // Get execution context
        ExecutionContext& context = m_stateManager->getExecutionContext(requestId);
        
        // Execute plan
        if (context.variables.find("execution_plan") != context.variables.end()) {
            nlohmann::json plan = context.variables["execution_plan"];
            
            if (m_errorRecoveryEnabled) {
                result = executeWithErrorRecovery(plan, requestId);
            } else {
                result = m_executionEngine->executeCommandSequence(plan["commands"], context);
            }
        } else {
            result.success = false;
            result.errorMessage = "No execution plan generated";
        }
        
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = "Exception during request processing: " + std::string(e.what());
        SLOG_ERROR().message(result.errorMessage);
    }
    
    return result;
}

TaskExecutionResult OrchestratorFacade::parseUserRequest(const std::string& userInput, const std::string& requestId) {
    TaskExecutionResult result;
    result.executionId = requestId;
    
    if (!m_commandParser) {
        // No parser, try direct LLM processing
        result.success = true;
        return result;
    }
    
    // Parse the user input
    auto parseResult = m_commandParser->parseUserInput(userInput);
    
    if (!parseResult.success) {
        result.success = false;
        result.errorMessage = parseResult.errorMessage;
        return result;
    }
    
    // Store parsed commands in context
    ExecutionContext& context = m_stateManager->getExecutionContext(requestId);
    // Convert vector<ParsedCommand> to json array
    nlohmann::json commandsArray = nlohmann::json::array();
    for (const auto& cmd : parseResult.commands) {
        nlohmann::json cmdJson = {
            {"command", cmd.action},
            {"parameters", cmd.parameters},
            {"description", cmd.description},
            {"priority", cmd.priority},
            {"isOptional", cmd.isOptional},
            {"delayAfterMs", cmd.delayAfterMs}
        };
        commandsArray.push_back(cmdJson);
    }
    context.variables["parsed_commands"] = commandsArray;
    context.variables["intent"] = {
        {"type", static_cast<int>(parseResult.intent.type)},
        {"confidence", static_cast<int>(parseResult.intent.confidence)}
    };
    
    result.success = true;
    return result;
}

TaskExecutionResult OrchestratorFacade::generateExecutionPlan(const std::string& userInput, const std::string& requestId) {
    TaskExecutionResult result;
    result.executionId = requestId;
    
    ExecutionContext& context = m_stateManager->getExecutionContext(requestId);
    
    // Check if we have parsed commands
    if (context.variables.find("parsed_commands") != context.variables.end()) {
        // Use parsed commands as plan
        nlohmann::json plan = {
            {"commands", context.variables["parsed_commands"]}
        };
        context.variables["execution_plan"] = plan;
        result.success = true;
        return result;
    }
    
    // Use LLM to generate plan
    if (!m_llmConnector) {
        result.success = false;
        result.errorMessage = "No LLM connector available for plan generation";
        return result;
    }
    
    // Initiate conversation for complex planning
    std::string conversationId = m_conversationManager->initiateConversation(userInput, context);
    
    // Wait for initial plan
    int attempts = 0;
    while (attempts < 10 && m_conversationManager->isConversationActive(conversationId)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Check if plan is available
        auto convContext = m_conversationManager->getConversationContext(conversationId);
        if (convContext.find("execution_plan") != convContext.end()) {
            context.variables["execution_plan"] = convContext["execution_plan"];
            result.success = true;
            break;
        }
        
        attempts++;
    }
    
    if (!result.success) {
        result.errorMessage = "Failed to generate execution plan";
    }
    
    return result;
}

TaskExecutionResult OrchestratorFacade::executeWithErrorRecovery(const nlohmann::json& plan, const std::string& requestId) {
    TaskExecutionResult result;
    ExecutionContext& context = m_stateManager->getExecutionContext(requestId);
    
    int retryCount = 0;
    
    while (retryCount <= m_maxErrorRetries) {
        // Execute plan
        result = m_executionEngine->executeCommandSequence(plan["commands"], context);
        
        if (result.success) {
            break;
        }
        
        // Attempt recovery
        SLOG_WARNING().message("Execution failed, attempting recovery")
                    .context("attempt", retryCount + 1)
                    .context("max_retries", m_maxErrorRetries);
        
        result = attemptErrorRecovery(requestId, result.errorMessage);
        
        if (!result.success) {
            break;
        }
        
        // Wait before retry
        std::this_thread::sleep_for(std::chrono::milliseconds(m_errorRecoveryDelayMs));
        
        retryCount++;
    }
    
    return result;
}

void OrchestratorFacade::handleExecutionError(const std::string& requestId, const std::string& error) {
    SLOG_ERROR().message("Execution error").context("request_id", requestId).context("error", error);
    
    // Raise error event
    m_eventManager->raiseEvent(OrchestratorEvent::ERROR_OCCURRED, error, requestId);
    
    // Update execution context
    ExecutionContext& context = m_stateManager->getExecutionContext(requestId);
    context.variables["last_error"] = error;
    int errorCount = 0;
    if (context.variables.find("error_count") != context.variables.end()) {
        errorCount = context.variables["error_count"].get<int>();
    }
    context.variables["error_count"] = errorCount + 1;
}

TaskExecutionResult OrchestratorFacade::attemptErrorRecovery(const std::string& requestId, const std::string& error) {
    TaskExecutionResult result;
    result.success = false;
    
    if (!m_conversationManager || !m_llmConnector) {
        result.errorMessage = "Error recovery not available";
        return result;
    }
    
    // Get execution context
    ExecutionContext& context = m_stateManager->getExecutionContext(requestId);
    
    // Create error context
    nlohmann::json errorContext = {
        {"error", error},
        {"original_request", context.originalRequest},
        {"execution_log", context.executionLog},
        {"variables", context.variables}
    };
    
    // Initiate error recovery conversation
    std::string conversationId = m_conversationManager->initiateErrorRecoveryConversation(
        context.originalRequest, errorContext);
    
    // Generate recovery plan
    result = m_conversationManager->generateRecoveryPlan(conversationId);
    
    if (result.success) {
        // Update execution plan with recovery plan
        try {
            nlohmann::json recoveryPlan = nlohmann::json::parse(result.output);
            context.variables["execution_plan"] = recoveryPlan;
            result.success = true;
        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = "Failed to parse recovery plan: " + std::string(e.what());
        }
    }
    
    return result;
}

void OrchestratorFacade::logActivity(const std::string& activity) {
    m_stateManager->logActivity(activity);
}

void OrchestratorFacade::updateSystemStatus() {
    // This could be expanded to update various system metrics
}

bool OrchestratorFacade::canProcessNewRequest() const {
    return m_isRunning && !m_isPaused && !m_emergencyStop &&
           m_stateManager->getActiveExecutionCount() < static_cast<size_t>(m_maxConcurrentTasks);
}

} // namespace burwell