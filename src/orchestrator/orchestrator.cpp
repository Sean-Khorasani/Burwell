#include "orchestrator.h"
#include "orchestrator_facade.h"
#include "../common/structured_logger.h"
#include "../common/error_handler.h"
#include "../common/config_manager.h"
#include "../cpl/cpl_config_loader.h"
#include "../environmental_perception/environmental_perception.h"
#include <memory>
#include <thread>

namespace burwell {

/**
 * @class Orchestrator
 * @brief Refactored orchestrator that delegates to the facade and components
 * 
 * This refactored version maintains the same public interface but internally
 * uses the new component-based architecture through OrchestratorFacade.
 */
class Orchestrator::OrchestratorImpl {
public:
    std::unique_ptr<OrchestratorFacade> facade;
    
    // Component references (maintained for compatibility)
    std::shared_ptr<CommandParser> commandParser;
    std::shared_ptr<LLMConnector> llmConnector;
    std::shared_ptr<TaskEngine> taskEngine;
    std::shared_ptr<OCAL> ocal;
    std::shared_ptr<EnvironmentalPerception> perception;
    std::shared_ptr<UIModule> ui;
};

Orchestrator::Orchestrator()
    : m_impl(std::make_unique<OrchestratorImpl>())
    , m_isRunning(false)
    , m_isPaused(false)
    , m_emergencyStop(false)
    , m_autoMode(false)
    , m_confirmationRequired(true)
    , m_maxConcurrentTasks(3) {
    
    // Create the facade
    m_impl->facade = std::make_unique<OrchestratorFacade>();
    
    // Load timing settings from CPL configuration
    auto& cplConfig = cpl::CPLConfigLoader::getInstance();
    m_executionTimeoutMs = cplConfig.getOrchestratorExecutionTimeout();
    m_mainLoopDelayMs = cplConfig.getOrchestratorMainLoopDelay();
    m_commandSequenceDelayMs = cplConfig.getOrchestratorCommandSequenceDelay();
    m_errorRecoveryDelayMs = cplConfig.getOrchestratorErrorRecoveryDelay();
    
    // Apply settings to facade
    m_impl->facade->setExecutionTimeout(m_executionTimeoutMs);
    m_impl->facade->setMainLoopDelayMs(m_mainLoopDelayMs);
    m_impl->facade->setCommandSequenceDelayMs(m_commandSequenceDelayMs);
    m_impl->facade->setErrorRecoveryDelayMs(m_errorRecoveryDelayMs);
    m_impl->facade->setMaxConcurrentTasks(m_maxConcurrentTasks);
    
    // Initialize resource monitor
    ResourceMonitor::getInstance().setEnabled(m_resourceThresholds.enableResourceTracking);
    
    SLOG_INFO().message("Orchestrator initialized with component-based architecture")
        .context("resource_tracking", m_resourceThresholds.enableResourceTracking);
}

Orchestrator::~Orchestrator() {
    shutdown();
}

void Orchestrator::initialize() {
    BURWELL_TRY_CATCH({
        SLOG_INFO().message("Initializing Orchestrator...");
        
        // Verify all components are available
        if (!m_impl->commandParser) {
            throw std::runtime_error("CommandParser not set");
        }
        if (!m_impl->llmConnector) {
            throw std::runtime_error("LLMConnector not set");
        }
        if (!m_impl->taskEngine) {
            throw std::runtime_error("TaskEngine not set");
        }
        if (!m_impl->ocal) {
            throw std::runtime_error("OCAL not set");
        }
        if (!m_impl->perception) {
            throw std::runtime_error("EnvironmentalPerception not set");
        }
        
        // Initialize facade with components
        m_impl->facade->setCommandParser(m_impl->commandParser);
        m_impl->facade->setLLMConnector(m_impl->llmConnector);
        m_impl->facade->setTaskEngine(m_impl->taskEngine);
        m_impl->facade->setOCAL(m_impl->ocal);
        m_impl->facade->setEnvironmentalPerception(m_impl->perception);
        if (m_impl->ui) {
            m_impl->facade->setUIModule(m_impl->ui);
        }
        
        // Initialize the facade
        m_impl->facade->initialize();
        
        m_isRunning = true;
        
        SLOG_INFO().message("Orchestrator initialization complete");
        
    }, "Orchestrator::initialize");
}

void Orchestrator::run() {
    BURWELL_TRY_CATCH({
        SLOG_INFO().message("Starting Orchestrator...");
        
        if (!m_isRunning) {
            initialize();
        }
        
        // Start the facade
        m_impl->facade->run();
        
        // Main loop for interactive mode
        while (m_isRunning && !m_emergencyStop) {
            if (m_isPaused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // Check if facade is idle and ready for new requests
            if (m_impl->facade->isIdle()) {
                // In interactive mode, this would wait for user input
                // For now, just sleep
                std::this_thread::sleep_for(std::chrono::milliseconds(m_mainLoopDelayMs));
            }
        }
        
    }, "Orchestrator::run");
}

void Orchestrator::shutdown() {
    BURWELL_TRY_CATCH({
        SLOG_INFO().message("Shutting down Orchestrator...");
        
        m_isRunning = false;
        
        // Shutdown the facade
        if (m_impl->facade) {
            m_impl->facade->shutdown();
        }
        
        SLOG_INFO().message("Orchestrator shutdown complete");
        
    }, "Orchestrator::shutdown");
}

TaskExecutionResult Orchestrator::processUserRequest(const std::string& userInput) {
    return m_impl->facade->processUserRequest(userInput);
}

TaskExecutionResult Orchestrator::executeTask(const std::string& taskId) {
    return m_impl->facade->executeTask(taskId);
}

TaskExecutionResult Orchestrator::executePlan(const nlohmann::json& executionPlan) {
    // Check resources before execution
    checkResourceUsage();
    if (!isResourceUsageAcceptable()) {
        SLOG_ERROR().message("Resource usage exceeds thresholds, aborting execution");
        TaskExecutionResult result;
        result.success = false;
        result.errorMessage = "Resource usage exceeds configured thresholds";
        return result;
    }
    
    // Log resource metrics before execution
    logResourceMetrics();
    
    // Execute the plan
    TaskExecutionResult result = m_impl->facade->executePlan(executionPlan);
    
    // Log resource metrics after execution
    logResourceMetrics();
    
    // Check for resource leaks or issues
    checkResourceUsage();
    
    return result;
}

TaskExecutionResult Orchestrator::executePlan(const nlohmann::json& executionPlan, ExecutionContext& context) {
    // Check resources before execution
    checkResourceUsage();
    if (!isResourceUsageAcceptable()) {
        SLOG_ERROR().message("Resource usage exceeds thresholds, aborting execution");
        TaskExecutionResult result;
        result.success = false;
        result.errorMessage = "Resource usage exceeds configured thresholds";
        return result;
    }
    
    // Log resource metrics with context
    SLOG_INFO().message("Resource metrics before command execution")
        .context("request_id", context.requestId);
    logResourceMetrics();
    
    // Store current context state
    std::string requestId = context.requestId;
    
    // Execute with facade
    TaskExecutionResult result = m_impl->facade->executePlan(executionPlan);
    
    // Update context with results
    if (result.success && !result.output.empty()) {
        try {
            nlohmann::json resultJson = nlohmann::json::parse(result.output);
            if (resultJson.contains("variables")) {
                for (const auto& [key, value] : resultJson["variables"].items()) {
                    context.variables[key] = value;
                }
            }
        } catch (...) {
            // Result output wasn't JSON, ignore
        }
    }
    
    // Log resource metrics after execution
    SLOG_INFO().message("Resource metrics after command execution")
        .context("request_id", context.requestId)
        .context("success", result.success);
    logResourceMetrics();
    
    // Check for resource issues
    checkResourceUsage();
    
    return result;
}

TaskExecutionResult Orchestrator::executeScriptFile(const std::string& scriptPath) {
    return m_impl->facade->executeScriptFile(scriptPath);
}

void Orchestrator::pauseExecution() {
    m_isPaused = true;
    m_impl->facade->pauseExecution();
}

void Orchestrator::resumeExecution() {
    m_isPaused = false;
    m_impl->facade->resumeExecution();
}

void Orchestrator::emergencyStop() {
    m_emergencyStop = true;
    m_impl->facade->emergencyStop();
}

bool Orchestrator::isRunning() const {
    return m_isRunning && m_impl->facade->isRunning();
}

bool Orchestrator::isPaused() const {
    return m_isPaused;
}

bool Orchestrator::isIdle() const {
    return m_impl->facade->isIdle();
}

void Orchestrator::setAutoMode(bool enabled) {
    m_autoMode = enabled;
    m_impl->facade->setAutoMode(enabled);
}

void Orchestrator::setConfirmationRequired(bool required) {
    m_confirmationRequired = required;
    m_impl->facade->setConfirmationRequired(required);
}

void Orchestrator::setMaxConcurrentTasks(int maxTasks) {
    m_maxConcurrentTasks = maxTasks;
    m_impl->facade->setMaxConcurrentTasks(maxTasks);
}

void Orchestrator::setExecutionTimeout(int timeoutMs) {
    m_executionTimeoutMs = timeoutMs;
    m_impl->facade->setExecutionTimeout(timeoutMs);
}

void Orchestrator::setCommandParser(std::shared_ptr<CommandParser> parser) {
    m_impl->commandParser = parser;
    if (m_impl->facade) {
        m_impl->facade->setCommandParser(parser);
    }
}

void Orchestrator::setLLMConnector(std::shared_ptr<LLMConnector> connector) {
    m_impl->llmConnector = connector;
    if (m_impl->facade) {
        m_impl->facade->setLLMConnector(connector);
    }
}

void Orchestrator::setTaskEngine(std::shared_ptr<TaskEngine> engine) {
    m_impl->taskEngine = engine;
    if (m_impl->facade) {
        m_impl->facade->setTaskEngine(engine);
    }
}

void Orchestrator::setOCAL(std::shared_ptr<OCAL> ocal) {
    m_impl->ocal = ocal;
    if (m_impl->facade) {
        m_impl->facade->setOCAL(ocal);
    }
}

void Orchestrator::setEnvironmentalPerception(std::shared_ptr<EnvironmentalPerception> perception) {
    m_impl->perception = perception;
    if (m_impl->facade) {
        m_impl->facade->setEnvironmentalPerception(perception);
    }
}

void Orchestrator::setUIModule(std::shared_ptr<UIModule> ui) {
    m_impl->ui = ui;
    if (m_impl->facade) {
        m_impl->facade->setUIModule(ui);
    }
}

nlohmann::json Orchestrator::getSystemStatus() const {
    return m_impl->facade->getSystemStatus();
}

nlohmann::json Orchestrator::getEnvironmentSnapshot() const {
    // Get from facade's feedback controller
    nlohmann::json status = m_impl->facade->getSystemStatus();
    
    // Add environment info if available
    if (m_impl->perception) {
        status["environment"] = m_impl->perception->gatherEnvironmentInfo();
    }
    
    return status;
}

std::vector<std::string> Orchestrator::getRecentActivity() const {
    return m_impl->facade->getRecentActivity();
}

void Orchestrator::enableFeedbackLoop(bool enable) {
    m_feedbackActive = enable;
    m_impl->facade->setFeedbackLoopEnabled(enable);
}

void Orchestrator::setEnvironmentCheckInterval(int intervalMs) {
    m_impl->facade->setEnvironmentCheckInterval(intervalMs);
}

// Legacy methods - maintained for compatibility but delegated to facade

void Orchestrator::logActivity(const std::string& activity) {
    // The facade handles activity logging internally
    SLOG_DEBUG().message(activity);
}

std::string Orchestrator::substituteVariables(const std::string& input, const ExecutionContext& context) {
    std::string result = input;
    
    // Simple variable substitution - in production, would use a proper parser
    for (const auto& [key, value] : context.variables) {
        std::string placeholder = "${" + key + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            std::string replacement;
            if (value.is_string()) {
                replacement = value.get<std::string>();
            } else {
                replacement = value.dump();
            }
            result.replace(pos, placeholder.length(), replacement);
            pos += replacement.length();
        }
    }
    
    return result;
}

TaskExecutionResult Orchestrator::executeSystemCommand(const nlohmann::json& command, ExecutionContext& context) {
    (void)context; // TODO: Use context in system command execution
    // Delegate to facade
    nlohmann::json plan = {
        {"commands", nlohmann::json::array({command})}
    };
    return m_impl->facade->executePlan(plan);
}

TaskExecutionResult Orchestrator::executeOCALCommand(const nlohmann::json& command, ExecutionContext& context) {
    (void)context; // TODO: Use context in OCAL command execution
    // Delegate to facade
    nlohmann::json plan = {
        {"commands", nlohmann::json::array({command})}
    };
    return m_impl->facade->executePlan(plan);
}

TaskExecutionResult Orchestrator::executeUIACommand(const nlohmann::json& command, ExecutionContext& context) {
    (void)context; // TODO: Use context in UIA command execution
    // Delegate to facade
    nlohmann::json plan = {
        {"commands", nlohmann::json::array({command})}
    };
    return m_impl->facade->executePlan(plan);
}

TaskExecutionResult Orchestrator::executeTaskCommand(const nlohmann::json& command, ExecutionContext& context) {
    (void)context; // TODO: Use context in task command execution
    // Extract task ID
    std::string taskId = command["params"].value("task_id", "");
    if (taskId.empty()) {
        TaskExecutionResult result;
        result.success = false;
        result.errorMessage = "No task_id specified";
        return result;
    }
    
    return m_impl->facade->executeTask(taskId);
}

TaskExecutionResult Orchestrator::executeScriptCommand(const nlohmann::json& command, ExecutionContext& context) {
    (void)context; // TODO: Use context in script command execution
    // Extract script path
    std::string scriptPath = command["params"].value("script_path", "");
    if (scriptPath.empty()) {
        TaskExecutionResult result;
        result.success = false;
        result.errorMessage = "No script_path specified";
        return result;
    }
    
    return m_impl->facade->executeScriptFile(scriptPath);
}

// Additional helper methods maintained for compatibility

bool Orchestrator::shouldContinueExecution() const {
    return m_isRunning && !m_emergencyStop && !m_isPaused;
}

void Orchestrator::handleExecutionError(const std::string& error, ExecutionContext& context) {
    SLOG_ERROR().message("Execution error")
        .context("error", error);
    context.executionLog.push_back("ERROR: " + error);
}

void Orchestrator::updateEnvironmentSnapshot(ExecutionContext& context) {
    if (m_impl->perception) {
        context.currentEnvironment = m_impl->perception->gatherEnvironmentInfo();
    }
}

bool Orchestrator::verifyCommandResult(const nlohmann::json& command, const TaskExecutionResult& result, ExecutionContext& context) {
    (void)command; // TODO: Use command for advanced verification
    (void)context; // TODO: Use context for verification
    // Basic verification - command completed without error
    return result.success;
}

// Resource monitoring methods
void Orchestrator::checkResourceUsage() {
    if (!m_resourceThresholds.enableResourceTracking) {
        return;
    }
    
    auto stats = ResourceMonitor::getInstance().getStats();
    
    // Check memory usage
    size_t memoryUsageMB = stats.totalMemoryUsage / (1024 * 1024);
    if (memoryUsageMB > m_resourceThresholds.maxMemoryUsageMB) {
        SLOG_WARNING().message("Memory usage exceeds threshold")
            .context("current_mb", memoryUsageMB)
            .context("threshold_mb", m_resourceThresholds.maxMemoryUsageMB);
    }
    
    // Check file handles
    if (stats.activeCount[ResourceMonitor::ResourceType::FILE_HANDLE] > m_resourceThresholds.maxFileHandles) {
        SLOG_WARNING().message("File handle count exceeds threshold")
            .context("current", stats.activeCount[ResourceMonitor::ResourceType::FILE_HANDLE])
            .context("threshold", m_resourceThresholds.maxFileHandles);
    }
    
    // Check window handles
    if (stats.activeCount[ResourceMonitor::ResourceType::WINDOW_HANDLE] > m_resourceThresholds.maxWindowHandles) {
        SLOG_WARNING().message("Window handle count exceeds threshold")
            .context("current", stats.activeCount[ResourceMonitor::ResourceType::WINDOW_HANDLE])
            .context("threshold", m_resourceThresholds.maxWindowHandles);
    }
    
    // Check threads
    if (stats.activeCount[ResourceMonitor::ResourceType::THREAD] > m_resourceThresholds.maxThreads) {
        SLOG_WARNING().message("Thread count exceeds threshold")
            .context("current", stats.activeCount[ResourceMonitor::ResourceType::THREAD])
            .context("threshold", m_resourceThresholds.maxThreads);
    }
}

void Orchestrator::logResourceMetrics() {
    if (!m_resourceThresholds.enableResourceTracking) {
        return;
    }
    
    auto stats = ResourceMonitor::getInstance().getStats();
    
    nlohmann::json metrics;
    metrics["memory_usage_mb"] = stats.totalMemoryUsage / (1024 * 1024);
    metrics["peak_memory_mb"] = stats.peakMemoryUsage / (1024 * 1024);
    
    // Active resource counts
    nlohmann::json activeResources;
    for (const auto& [type, count] : stats.activeCount) {
        std::string typeName;
        switch (type) {
            case ResourceMonitor::ResourceType::MEMORY: typeName = "memory"; break;
            case ResourceMonitor::ResourceType::FILE_HANDLE: typeName = "file_handles"; break;
            case ResourceMonitor::ResourceType::WINDOW_HANDLE: typeName = "window_handles"; break;
            case ResourceMonitor::ResourceType::PROCESS_HANDLE: typeName = "process_handles"; break;
            case ResourceMonitor::ResourceType::THREAD: typeName = "threads"; break;
            case ResourceMonitor::ResourceType::MUTEX: typeName = "mutexes"; break;
            default: typeName = "other"; break;
        }
        activeResources[typeName] = count;
    }
    metrics["active_resources"] = activeResources;
    
    // Peak usage
    nlohmann::json peakUsage;
    for (const auto& [type, count] : stats.peakUsage) {
        std::string typeName;
        switch (type) {
            case ResourceMonitor::ResourceType::MEMORY: typeName = "memory"; break;
            case ResourceMonitor::ResourceType::FILE_HANDLE: typeName = "file_handles"; break;
            case ResourceMonitor::ResourceType::WINDOW_HANDLE: typeName = "window_handles"; break;
            case ResourceMonitor::ResourceType::PROCESS_HANDLE: typeName = "process_handles"; break;
            case ResourceMonitor::ResourceType::THREAD: typeName = "threads"; break;
            case ResourceMonitor::ResourceType::MUTEX: typeName = "mutexes"; break;
            default: typeName = "other"; break;
        }
        peakUsage[typeName] = count;
    }
    metrics["peak_usage"] = peakUsage;
    
    SLOG_INFO().message("Resource usage metrics")
        .context("metrics", metrics);
}

bool Orchestrator::isResourceUsageAcceptable() const {
    if (!m_resourceThresholds.enableResourceTracking) {
        return true;
    }
    
    auto stats = ResourceMonitor::getInstance().getStats();
    
    // Check all thresholds
    size_t memoryUsageMB = stats.totalMemoryUsage / (1024 * 1024);
    if (memoryUsageMB > m_resourceThresholds.maxMemoryUsageMB) {
        return false;
    }
    
    if (stats.activeCount.count(ResourceMonitor::ResourceType::FILE_HANDLE) &&
        stats.activeCount.at(ResourceMonitor::ResourceType::FILE_HANDLE) > m_resourceThresholds.maxFileHandles) {
        return false;
    }
    
    if (stats.activeCount.count(ResourceMonitor::ResourceType::WINDOW_HANDLE) &&
        stats.activeCount.at(ResourceMonitor::ResourceType::WINDOW_HANDLE) > m_resourceThresholds.maxWindowHandles) {
        return false;
    }
    
    if (stats.activeCount.count(ResourceMonitor::ResourceType::THREAD) &&
        stats.activeCount.at(ResourceMonitor::ResourceType::THREAD) > m_resourceThresholds.maxThreads) {
        return false;
    }
    
    return true;
}

} // namespace burwell