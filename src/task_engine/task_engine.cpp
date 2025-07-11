#include "task_engine.h"
#include "../common/structured_logger.h"
#include "../common/error_handler.h"
#include "../common/os_utils.h"
#include "../common/file_utils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <chrono>
#include <regex>
#include <thread>

using namespace burwell;

// TaskParameter implementations
nlohmann::json TaskParameter::toJson() const {
    nlohmann::json json;
    json["name"] = name;
    json["type"] = type;
    json["defaultValue"] = defaultValue;
    json["description"] = description;
    json["required"] = required;
    json["allowedValues"] = allowedValues;
    return json;
}

void TaskParameter::fromJson(const nlohmann::json& json) {
    if (json.contains("name")) name = json["name"];
    if (json.contains("type")) type = json["type"];
    if (json.contains("defaultValue")) defaultValue = json["defaultValue"];
    if (json.contains("description")) description = json["description"];
    if (json.contains("required")) required = json["required"];
    if (json.contains("allowedValues")) allowedValues = json["allowedValues"];
}

// TaskCommand implementations
nlohmann::json TaskCommand::toJson() const {
    nlohmann::json json;
    json["command"] = command;
    json["params"] = params;
    json["description"] = description;
    json["delayAfterMs"] = delayAfterMs;
    json["optional"] = optional;
    return json;
}

void TaskCommand::fromJson(const nlohmann::json& json) {
    if (json.contains("command")) command = json["command"];
    if (json.contains("params")) params = json["params"];
    if (json.contains("description")) description = json["description"];
    if (json.contains("delayAfterMs")) delayAfterMs = json["delayAfterMs"];
    if (json.contains("optional")) optional = json["optional"];
}

// TaskDefinition implementations
bool TaskDefinition::isValid() const {
    if (name.empty() || version.empty()) return false;
    if (commands.empty()) return false;
    
    // Check for required parameters
    for (const auto& param : parameters) {
        if (param.required && param.name.empty()) return false;
    }
    
    // Check command validity
    for (const auto& cmd : commands) {
        if (cmd.command.empty()) return false;
    }
    
    return true;
}

nlohmann::json TaskDefinition::toJson() const {
    nlohmann::json json;
    json["name"] = name;
    json["version"] = version;
    json["description"] = description;
    json["author"] = author;
    json["category"] = category;
    json["tags"] = tags;
    
    json["parameters"] = nlohmann::json::array();
    for (const auto& param : parameters) {
        json["parameters"].push_back(param.toJson());
    }
    
    json["commands"] = nlohmann::json::array();
    for (const auto& cmd : commands) {
        json["commands"].push_back(cmd.toJson());
    }
    
    json["rollbackCommands"] = nlohmann::json::array();
    for (const auto& cmd : rollbackCommands) {
        json["rollbackCommands"].push_back(cmd.toJson());
    }
    
    json["metadata"] = metadata;
    json["timeoutMs"] = timeoutMs;
    json["requiresConfirmation"] = requiresConfirmation;
    json["allowParallelExecution"] = allowParallelExecution;
    json["dependencies"] = dependencies;
    
    return json;
}

void TaskDefinition::fromJson(const nlohmann::json& json) {
    if (json.contains("name")) name = json["name"];
    if (json.contains("version")) version = json["version"];
    if (json.contains("description")) description = json["description"];
    if (json.contains("author")) author = json["author"];
    if (json.contains("category")) category = json["category"];
    if (json.contains("tags")) tags = json["tags"];
    
    if (json.contains("parameters")) {
        parameters.clear();
        for (const auto& paramJson : json["parameters"]) {
            TaskParameter param;
            param.fromJson(paramJson);
            parameters.push_back(param);
        }
    }
    
    if (json.contains("commands")) {
        commands.clear();
        for (const auto& cmdJson : json["commands"]) {
            TaskCommand cmd;
            cmd.fromJson(cmdJson);
            commands.push_back(cmd);
        }
    }
    
    if (json.contains("rollbackCommands")) {
        rollbackCommands.clear();
        for (const auto& cmdJson : json["rollbackCommands"]) {
            TaskCommand cmd;
            cmd.fromJson(cmdJson);
            rollbackCommands.push_back(cmd);
        }
    }
    
    if (json.contains("metadata")) metadata = json["metadata"];
    if (json.contains("timeoutMs")) timeoutMs = json["timeoutMs"];
    if (json.contains("requiresConfirmation")) requiresConfirmation = json["requiresConfirmation"];
    if (json.contains("allowParallelExecution")) allowParallelExecution = json["allowParallelExecution"];
    if (json.contains("dependencies")) dependencies = json["dependencies"];
}

std::string TaskDefinition::getFilePath() const {
    return name + "_" + version + ".json";
}

struct TaskEngine::TaskEngineImpl {
    // Placeholder for future implementation details
    std::string placeholder;
};

// TaskEngine implementation
TaskEngine::TaskEngine()
    : m_impl(std::make_unique<TaskEngineImpl>())
    , m_taskLibraryPath("./tasks")
    , m_autoSave(true)
    , m_versioning(true)
    , m_maxExecutionHistory(100)
    , m_defaultTimeoutMs(300000) {
    
    // Load existing tasks from disk
    loadTasksFromDisk();
    
    SLOG_INFO().message("TaskEngine initialized")
        .context("task_count", m_tasks.size());
}

TaskEngine::~TaskEngine() = default;

bool TaskEngine::saveTask(const TaskDefinition& task) {
    BURWELL_TRY_CATCH({
        if (!validateTask(task)) {
            SLOG_ERROR().message("Invalid task definition")
                .context("task_name", task.name);
            return false;
        }
        
        // Save to memory
        m_tasks[task.name] = task;
        
        // Save versioned copy if versioning enabled
        if (m_versioning) {
            m_taskVersions[task.name].push_back(task);
        }
        
        // Save to disk if auto-save enabled
        if (m_autoSave) {
            return saveTaskToDisk(task);
        }
        
        return true;
    }, "TaskEngine::saveTask");
    
    return false;
}

bool TaskEngine::saveTask(const std::string& taskName, const std::string& taskDefinition) {
    BURWELL_TRY_CATCH({
        nlohmann::json taskJson = nlohmann::json::parse(taskDefinition);
        TaskDefinition task;
        task.fromJson(taskJson);
        task.name = taskName; // Ensure name matches
        
        return saveTask(task);
    }, "TaskEngine::saveTask(string)");
    
    return false;
}

TaskDefinition TaskEngine::getTaskDefinition(const std::string& taskName) {
    auto it = m_tasks.find(taskName);
    if (it != m_tasks.end()) {
        return it->second;
    }
    return TaskDefinition();
}

std::string TaskEngine::getTask(const std::string& taskName) {
    TaskDefinition task = getTaskDefinition(taskName);
    if (!task.name.empty()) {
        return task.toJson().dump(2);
    }
    return "";
}

bool TaskEngine::deleteTask(const std::string& taskName) {
    BURWELL_TRY_CATCH({
        auto it = m_tasks.find(taskName);
        if (it == m_tasks.end()) {
            return false;
        }
        
        // Remove from memory
        m_tasks.erase(it);
        
        // Remove versions
        m_taskVersions.erase(taskName);
        
        // Remove from disk
        return deleteTaskFromDisk(taskName);
    }, "TaskEngine::deleteTask");
    
    return false;
}

bool TaskEngine::taskExists(const std::string& taskName) {
    return m_tasks.find(taskName) != m_tasks.end();
}

std::vector<std::string> TaskEngine::listTasks() {
    std::vector<std::string> taskNames;
    for (const auto& pair : m_tasks) {
        taskNames.push_back(pair.first);
    }
    std::sort(taskNames.begin(), taskNames.end());
    return taskNames;
}

std::vector<std::string> TaskEngine::findTasksByCategory(const std::string& category) {
    std::vector<std::string> results;
    for (const auto& pair : m_tasks) {
        if (pair.second.category == category) {
            results.push_back(pair.first);
        }
    }
    return results;
}

std::vector<std::string> TaskEngine::findTasksByTag(const std::string& tag) {
    std::vector<std::string> results;
    for (const auto& pair : m_tasks) {
        auto& tags = pair.second.tags;
        if (std::find(tags.begin(), tags.end(), tag) != tags.end()) {
            results.push_back(pair.first);
        }
    }
    return results;
}

std::vector<std::string> TaskEngine::searchTasks(const std::string& query) {
    std::vector<std::string> results;
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    
    for (const auto& pair : m_tasks) {
        const TaskDefinition& task = pair.second;
        
        // Search in name, description, and tags
        std::string searchText = task.name + " " + task.description;
        for (const auto& tag : task.tags) {
            searchText += " " + tag;
        }
        
        std::transform(searchText.begin(), searchText.end(), searchText.begin(), ::tolower);
        
        if (searchText.find(lowerQuery) != std::string::npos) {
            results.push_back(pair.first);
        }
    }
    
    return results;
}

std::vector<TaskDefinition> TaskEngine::getAllTasks() {
    std::vector<TaskDefinition> tasks;
    for (const auto& pair : m_tasks) {
        tasks.push_back(pair.second);
    }
    return tasks;
}

TaskExecutionResult TaskEngine::executeTask(const std::string& taskName, const std::map<std::string, std::string>& params) {
    TaskDefinition task = getTaskDefinition(taskName);
    if (task.name.empty()) {
        TaskExecutionResult result;
        result.errorMessage = "Task not found: " + taskName;
        return result;
    }
    
    return executeTaskDefinition(task, params);
}

TaskExecutionResult TaskEngine::executeTaskDefinition(const TaskDefinition& task, const std::map<std::string, std::string>& params) {
    TaskExecutionResult result;
    
    BURWELL_TRY_CATCH({
        if (!validateTask(task)) {
            result.errorMessage = "Invalid task definition";
            return result;
        }
        
        if (!validateParameters(task, params)) {
            result.errorMessage = "Invalid parameters";
            return result;
        }
        
        // Create execution context
        TaskExecutionContext context;
        context.taskName = task.name;
        context.parameters = processParameters(task, params);
        context.executionId = generateExecutionId();
        context.startTime = std::chrono::system_clock::now();
        context.isRunning = true;
        
        result.executionId = context.executionId;
        
        // Add to active executions
        m_activeExecutions.push_back(context);
        
        // Execute commands
        bool executionSuccess = true;
        for (size_t i = 0; i < task.commands.size(); ++i) {
            context.currentCommandIndex = i;
            
            if (!executeCommand(task.commands[i], context)) {
                if (!task.commands[i].optional) {
                    executionSuccess = false;
                    break;
                }
            }
            
            // Add delay if specified
            if (task.commands[i].delayAfterMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(task.commands[i].delayAfterMs));
            }
        }
        
        // Complete execution
        context.endTime = std::chrono::system_clock::now();
        context.isRunning = false;
        context.successful = executionSuccess;
        
        if (!executionSuccess) {
            context.errorMessage = "Command execution failed";
            executeRollback(task, context);
        }
        
        // Remove from active executions
        m_activeExecutions.erase(
            std::remove_if(m_activeExecutions.begin(), m_activeExecutions.end(),
                          [&context](const TaskExecutionContext& ctx) { 
                              return ctx.executionId == context.executionId; 
                          }),
            m_activeExecutions.end()
        );
        
        // Create result
        result.success = executionSuccess;
        result.errorMessage = context.errorMessage;
        result.executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            context.endTime - context.startTime);
        result.executedCommands = context.executedCommands;
        
        // Add to history
        m_executionHistory.push_back(result);
        if (m_executionHistory.size() > m_maxExecutionHistory) {
            m_executionHistory.erase(m_executionHistory.begin());
        }
        
        // Update statistics
        updateStatistics(result);
        
        // Notify event callback
        notifyEvent(executionSuccess ? "EXECUTION_COMPLETED" : "EXECUTION_FAILED", context);
        
    }, "TaskEngine::executeTaskDefinition");
    
    return result;
}

bool TaskEngine::cancelExecution(const std::string& executionId) {
    for (auto& context : m_activeExecutions) {
        if (context.executionId == executionId) {
            context.isRunning = false;
            context.successful = false;
            context.errorMessage = "Execution cancelled";
            return true;
        }
    }
    return false;
}

bool TaskEngine::validateTask(const TaskDefinition& task) {
    return task.isValid();
}

std::vector<std::string> TaskEngine::getValidationErrors(const TaskDefinition& task) {
    std::vector<std::string> errors;
    
    if (task.name.empty()) {
        errors.push_back("Task name is required");
    }
    
    if (task.version.empty()) {
        errors.push_back("Task version is required");
    }
    
    if (task.commands.empty()) {
        errors.push_back("At least one command is required");
    }
    
    for (const auto& param : task.parameters) {
        if (param.required && param.name.empty()) {
            errors.push_back("Required parameter must have a name");
        }
    }
    
    for (const auto& cmd : task.commands) {
        if (cmd.command.empty()) {
            errors.push_back("Command name cannot be empty");
        }
    }
    
    return errors;
}

bool TaskEngine::validateParameters(const TaskDefinition& task, const std::map<std::string, std::string>& params) {
    for (const auto& param : task.parameters) {
        if (param.required) {
            auto it = params.find(param.name);
            if (it == params.end() || it->second.empty()) {
                if (param.defaultValue.empty()) {
                    return false;
                }
            }
        }
        
        // Validate parameter type if provided
        auto it = params.find(param.name);
        if (it != params.end()) {
            if (!validateParameterType(param, it->second)) {
                return false;
            }
        }
    }
    
    return true;
}

// Implementation for remaining methods
bool TaskEngine::loadTasksFromDisk() {
    BURWELL_TRY_CATCH({
        if (!std::filesystem::exists(m_taskLibraryPath)) {
            std::filesystem::create_directories(m_taskLibraryPath);
            createDefaultTasks();
            return true;
        }
        
        auto jsonFiles = listJsonFiles(m_taskLibraryPath);
        for (const auto& file : jsonFiles) {
            if (isValidTaskFile(file)) {
                nlohmann::json taskJson;
                if (utils::FileUtils::loadJsonFromFile(file, taskJson)) {
                    TaskDefinition task;
                    task.fromJson(taskJson);
                    
                    if (task.isValid()) {
                        m_tasks[task.name] = task;
                        SLOG_DEBUG().message("Loaded task from file")
                            .context("task", task.name)
                            .context("file", file);
                    } else {
                        SLOG_WARNING().message("Invalid task in file")
                            .context("file", file);
                    }
                } else {
                    SLOG_WARNING().message("Failed to load task file")
                        .context("file", file);
                }
            }
        }
        
        SLOG_INFO().message("Loaded tasks from disk")
            .context("task_count", m_tasks.size());
        return true;
    }, "TaskEngine::loadTasksFromDisk");
    
    return false;
}

bool TaskEngine::saveTaskToDisk(const TaskDefinition& task) {
    BURWELL_TRY_CATCH({
        ensureDirectoryExists(m_taskLibraryPath);
        
        std::string filePath = getTaskFilePath(task.name, task.version);
        return utils::FileUtils::saveJsonToFile(filePath, task.toJson());
    }, "TaskEngine::saveTaskToDisk");
    
    return false;
}

bool TaskEngine::deleteTaskFromDisk(const std::string& taskName) {
    std::string filePath = getTaskFilePath(taskName);
    return std::filesystem::remove(filePath);
}

std::string TaskEngine::generateExecutionId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    return std::to_string(timestamp) + "_" + std::to_string(dis(gen));
}

std::string TaskEngine::getTaskFilePath(const std::string& taskName, const std::string& version) {
    std::string filename = taskName;
    if (!version.empty()) {
        filename += "_" + version;
    }
    filename += ".json";
    
    return burwell::os::PathUtils::toNativePath((std::filesystem::path(m_taskLibraryPath) / filename).string());
}

void TaskEngine::updateStatistics(const TaskExecutionResult& result) {
    auto& stats = m_taskStatistics[result.executionId]; // This should use task name, not execution ID
    // Note: This is a simplified implementation - in reality we'd track by task name
    stats.totalExecutions++;
    if (result.success) {
        stats.successfulExecutions++;
    } else {
        stats.failedExecutions++;
    }
    stats.lastExecuted = std::chrono::system_clock::now();
}

void TaskEngine::notifyEvent(const std::string& event, const TaskExecutionContext& context) {
    if (m_eventCallback) {
        m_eventCallback(event, context);
    }
}

std::map<std::string, std::string> TaskEngine::processParameters(const TaskDefinition& task, const std::map<std::string, std::string>& inputParams) {
    std::map<std::string, std::string> processedParams;
    
    for (const auto& param : task.parameters) {
        std::string value = getParameterValue(param, inputParams);
        processedParams[param.name] = value;
    }
    
    return processedParams;
}

bool TaskEngine::validateParameterType(const TaskParameter& param, const std::string& value) {
    if (param.type == "number") {
        try {
            std::stod(value);
        } catch (...) {
            return false;
        }
    } else if (param.type == "boolean") {
        if (value != "true" && value != "false" && value != "1" && value != "0") {
            return false;
        }
    }
    
    // Check allowed values
    if (!param.allowedValues.empty()) {
        return std::find(param.allowedValues.begin(), param.allowedValues.end(), value) != param.allowedValues.end();
    }
    
    return true;
}

std::string TaskEngine::getParameterValue(const TaskParameter& param, const std::map<std::string, std::string>& inputParams) {
    auto it = inputParams.find(param.name);
    if (it != inputParams.end() && !it->second.empty()) {
        return it->second;
    }
    return param.defaultValue;
}

bool TaskEngine::executeCommand(const TaskCommand& command, TaskExecutionContext& context) {
    if (m_commandExecutor) {
        // Inject parameters into command params
        nlohmann::json processedParams = injectParametersIntoJson(command.params, context.parameters);
        
        bool success = m_commandExecutor(command.command, processedParams);
        
        if (success) {
            context.executedCommands.push_back(command.command);
        }
        
        return success;
    }
    
    // Default implementation - just log the command
    SLOG_DEBUG().message("Executing command")
        .context("command", command.command);
    context.executedCommands.push_back(command.command);
    return true;
}

bool TaskEngine::executeRollback(const TaskDefinition& task, TaskExecutionContext& context) {
    for (const auto& command : task.rollbackCommands) {
        executeCommand(command, context);
    }
    return true;
}

std::string TaskEngine::injectParameters(const std::string& text, const std::map<std::string, std::string>& params) {
    std::string result = text;
    
    for (const auto& param : params) {
        std::string placeholder = "${" + param.first + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), param.second);
            pos += param.second.length();
        }
    }
    
    return result;
}

nlohmann::json TaskEngine::injectParametersIntoJson(const nlohmann::json& json, const std::map<std::string, std::string>& params) {
    if (json.is_string()) {
        return injectParameters(json.get<std::string>(), params);
    } else if (json.is_object()) {
        nlohmann::json result = nlohmann::json::object();
        for (auto it = json.begin(); it != json.end(); ++it) {
            result[it.key()] = injectParametersIntoJson(it.value(), params);
        }
        return result;
    } else if (json.is_array()) {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : json) {
            result.push_back(injectParametersIntoJson(item, params));
        }
        return result;
    }
    
    return json;
}

void TaskEngine::ensureDirectoryExists(const std::string& path) {
    std::filesystem::create_directories(path);
}

std::vector<std::string> TaskEngine::listJsonFiles(const std::string& directory) {
    std::vector<std::string> files;
    
    if (std::filesystem::exists(directory)) {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().extension() == ".json") {
                files.push_back(burwell::os::PathUtils::toNativePath(entry.path().string()));
            }
        }
    }
    
    return files;
}

bool TaskEngine::isValidTaskFile(const std::string& filePath) {
    return std::filesystem::path(filePath).extension() == ".json";
}

void TaskEngine::createDefaultTasks() {
    TaskDefinition exampleTask = createExampleTask();
    saveTask(exampleTask);
}

TaskDefinition TaskEngine::createExampleTask() {
    TaskDefinition task;
    task.name = "example_task";
    task.version = "1.0.0";
    task.description = "Example task demonstrating Burwell task format";
    task.author = "Burwell";
    task.category = "examples";
    task.tags = {"example", "demo"};
    
    TaskParameter param;
    param.name = "message";
    param.type = "string";
    param.defaultValue = "Hello World";
    param.description = "Message to display";
    param.required = false;
    task.parameters.push_back(param);
    
    TaskCommand command;
    command.command = "system.sleep";
    command.params = nlohmann::json{{"ms", 1000}};
    command.description = "Wait for 1 second";
    task.commands.push_back(command);
    
    return task;
}

// Configuration methods
void TaskEngine::setTaskLibraryPath(const std::string& path) { m_taskLibraryPath = path; }
std::string TaskEngine::getTaskLibraryPath() const { return m_taskLibraryPath; }
void TaskEngine::setAutoSave(bool enable) { m_autoSave = enable; }
void TaskEngine::setVersioning(bool enable) { m_versioning = enable; }
void TaskEngine::setMaxExecutionHistory(size_t maxHistory) { m_maxExecutionHistory = maxHistory; }
void TaskEngine::setDefaultTimeout(int timeoutMs) { m_defaultTimeoutMs = timeoutMs; }
void TaskEngine::setTaskEventCallback(TaskEventCallback callback) { m_eventCallback = callback; }
void TaskEngine::setCommandExecutor(CommandExecutor executor) { m_commandExecutor = executor; }

// Simple stub implementations for remaining methods
bool TaskEngine::saveTaskVersion(const TaskDefinition& task) { return saveTask(task); }
std::vector<std::string> TaskEngine::getTaskVersions(const std::string& taskName) {
    (void)taskName; // TODO: Implement version retrieval
    return {};
}
TaskDefinition TaskEngine::getTaskVersion(const std::string& taskName, const std::string& version) {
    (void)taskName; // TODO: Implement specific version retrieval
    (void)version; // TODO: Implement specific version retrieval
    return TaskDefinition();
}
bool TaskEngine::promoteTaskVersion(const std::string& taskName, const std::string& version) {
    (void)taskName; // TODO: Implement version promotion
    (void)version; // TODO: Implement version promotion
    return false;
}
bool TaskEngine::checkDependencies(const TaskDefinition& task) {
    (void)task; // TODO: Implement dependency checking
    return true;
}
std::vector<std::string> TaskEngine::getMissingDependencies(const TaskDefinition& task) {
    (void)task; // TODO: Implement missing dependency detection
    return {};
}
std::vector<std::string> TaskEngine::getTaskDependents(const std::string& taskName) {
    (void)taskName; // TODO: Implement dependent task retrieval
    return {};
}
std::vector<TaskExecutionContext> TaskEngine::getActiveExecutions() { return m_activeExecutions; }
TaskExecutionContext TaskEngine::getExecutionStatus(const std::string& executionId) {
    (void)executionId; // TODO: Implement execution status retrieval
    return TaskExecutionContext();
}
std::vector<TaskExecutionResult> TaskEngine::getExecutionHistory(const std::string& taskName) {
    (void)taskName; // TODO: Filter history by task name
    return m_executionHistory;
}
void TaskEngine::clearExecutionHistory() { m_executionHistory.clear(); }
bool TaskEngine::importTask(const std::string& filePath) {
    (void)filePath; // TODO: Implement task import from file
    return false;
}
bool TaskEngine::exportTask(const std::string& taskName, const std::string& filePath) {
    (void)taskName; // TODO: Implement task export to file
    (void)filePath; // TODO: Implement task export to file
    return false;
}
bool TaskEngine::importTaskLibrary(const std::string& directoryPath) {
    (void)directoryPath; // TODO: Implement task library import
    return false;
}
bool TaskEngine::exportTaskLibrary(const std::string& directoryPath) {
    (void)directoryPath; // TODO: Implement task library export
    return false;
}
TaskEngine::TaskStatistics TaskEngine::getTaskStatistics(const std::string& taskName) {
    (void)taskName; // TODO: Implement task statistics retrieval
    return TaskStatistics();
}
std::vector<TaskEngine::TaskStatistics> TaskEngine::getAllTaskStatistics() { return {}; }


