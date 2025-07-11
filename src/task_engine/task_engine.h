#ifndef BURWELL_TASK_ENGINE_H
#define BURWELL_TASK_ENGINE_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>
#include "../common/types.h"

namespace burwell {

struct TaskParameter {
    std::string name;
    std::string type;           // "string", "number", "boolean", "array", "object"
    std::string defaultValue;
    std::string description;
    bool required;
    std::vector<std::string> allowedValues; // For enum-like parameters
    
    TaskParameter() : required(false) {}
    
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& json);
};

struct TaskCommand {
    std::string command;        // e.g., "mouse.click", "keyboard.type"
    nlohmann::json params;      // Command parameters
    std::string description;    // Optional description
    int delayAfterMs;          // Delay after execution (milliseconds)
    bool optional;             // Can this command fail without stopping execution?
    
    TaskCommand() : delayAfterMs(0), optional(false) {}
    
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& json);
};

struct TaskDefinition {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::string category;
    std::vector<std::string> tags;
    std::vector<TaskParameter> parameters;
    std::vector<TaskCommand> commands;
    std::vector<TaskCommand> rollbackCommands;
    std::map<std::string, std::string> metadata;
    
    // Execution settings
    int timeoutMs;
    bool requiresConfirmation;
    bool allowParallelExecution;
    std::vector<std::string> dependencies; // Other tasks that must be available
    
    TaskDefinition() : timeoutMs(300000), requiresConfirmation(false), allowParallelExecution(true) {}
    
    bool isValid() const;
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& json);
    std::string getFilePath() const;
};

struct TaskExecutionContext {
    std::string taskName;
    std::map<std::string, std::string> parameters;
    std::string executionId;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    bool isRunning;
    bool successful;
    std::string errorMessage;
    std::vector<std::string> executedCommands;
    int currentCommandIndex;
    
    TaskExecutionContext() : isRunning(false), successful(false), currentCommandIndex(0) {}
};

// TaskExecutionResult is now defined in orchestrator.h to avoid duplicates

class TaskEngine {
public:
    TaskEngine();
    ~TaskEngine();
    
    // Task management
    bool saveTask(const TaskDefinition& task);
    bool saveTask(const std::string& taskName, const std::string& taskDefinition);
    TaskDefinition getTaskDefinition(const std::string& taskName);
    std::string getTask(const std::string& taskName); // Legacy interface
    bool deleteTask(const std::string& taskName);
    bool taskExists(const std::string& taskName);
    
    // Task discovery and search
    std::vector<std::string> listTasks();
    std::vector<std::string> findTasksByCategory(const std::string& category);
    std::vector<std::string> findTasksByTag(const std::string& tag);
    std::vector<std::string> searchTasks(const std::string& query);
    std::vector<TaskDefinition> getAllTasks();
    
    // Task execution
    TaskExecutionResult executeTask(const std::string& taskName, const std::map<std::string, std::string>& params = {});
    TaskExecutionResult executeTaskDefinition(const TaskDefinition& task, const std::map<std::string, std::string>& params = {});
    bool cancelExecution(const std::string& executionId);
    
    // Task validation
    bool validateTask(const TaskDefinition& task);
    std::vector<std::string> getValidationErrors(const TaskDefinition& task);
    bool validateParameters(const TaskDefinition& task, const std::map<std::string, std::string>& params);
    
    // Task versioning
    bool saveTaskVersion(const TaskDefinition& task);
    std::vector<std::string> getTaskVersions(const std::string& taskName);
    TaskDefinition getTaskVersion(const std::string& taskName, const std::string& version);
    bool promoteTaskVersion(const std::string& taskName, const std::string& version);
    
    // Task dependencies
    bool checkDependencies(const TaskDefinition& task);
    std::vector<std::string> getMissingDependencies(const TaskDefinition& task);
    std::vector<std::string> getTaskDependents(const std::string& taskName);
    
    // Execution monitoring
    std::vector<TaskExecutionContext> getActiveExecutions();
    TaskExecutionContext getExecutionStatus(const std::string& executionId);
    std::vector<TaskExecutionResult> getExecutionHistory(const std::string& taskName = "");
    void clearExecutionHistory();
    
    // Configuration
    void setTaskLibraryPath(const std::string& path);
    std::string getTaskLibraryPath() const;
    void setAutoSave(bool enable);
    void setVersioning(bool enable);
    void setMaxExecutionHistory(size_t maxHistory);
    void setDefaultTimeout(int timeoutMs);
    
    // Import/Export
    bool importTask(const std::string& filePath);
    bool exportTask(const std::string& taskName, const std::string& filePath);
    bool importTaskLibrary(const std::string& directoryPath);
    bool exportTaskLibrary(const std::string& directoryPath);
    
    // Event handling
    using TaskEventCallback = std::function<void(const std::string& event, const TaskExecutionContext& context)>;
    void setTaskEventCallback(TaskEventCallback callback);
    
    // Statistics and analytics
    struct TaskStatistics {
        std::string taskName;
        int totalExecutions;
        int successfulExecutions;
        int failedExecutions;
        std::chrono::milliseconds averageExecutionTime;
        std::chrono::system_clock::time_point lastExecuted;
    };
    
    TaskStatistics getTaskStatistics(const std::string& taskName);
    std::vector<TaskStatistics> getAllTaskStatistics();
    
    // Command execution interface
    using CommandExecutor = std::function<bool(const std::string& command, const nlohmann::json& params)>;
    void setCommandExecutor(CommandExecutor executor);

private:
    struct TaskEngineImpl;
    std::unique_ptr<TaskEngineImpl> m_impl;
    
    // Configuration
    std::string m_taskLibraryPath;
    bool m_autoSave;
    bool m_versioning;
    size_t m_maxExecutionHistory;
    int m_defaultTimeoutMs;
    
    // Task storage
    std::map<std::string, TaskDefinition> m_tasks;
    std::map<std::string, std::vector<TaskDefinition>> m_taskVersions;
    
    // Execution tracking
    std::vector<TaskExecutionContext> m_activeExecutions;
    std::vector<TaskExecutionResult> m_executionHistory;
    
    // Statistics
    std::map<std::string, TaskStatistics> m_taskStatistics;
    
    // Callbacks
    TaskEventCallback m_eventCallback;
    CommandExecutor m_commandExecutor;
    
    // Internal methods
    bool loadTasksFromDisk();
    bool saveTaskToDisk(const TaskDefinition& task);
    bool deleteTaskFromDisk(const std::string& taskName);
    std::string generateExecutionId();
    std::string getTaskFilePath(const std::string& taskName, const std::string& version = "");
    void updateStatistics(const TaskExecutionResult& result);
    void notifyEvent(const std::string& event, const TaskExecutionContext& context);
    
    // Parameter injection and validation
    std::map<std::string, std::string> processParameters(const TaskDefinition& task, const std::map<std::string, std::string>& inputParams);
    bool validateParameterType(const TaskParameter& param, const std::string& value);
    std::string getParameterValue(const TaskParameter& param, const std::map<std::string, std::string>& inputParams);
    
    // Command execution
    bool executeCommand(const TaskCommand& command, TaskExecutionContext& context);
    bool executeRollback(const TaskDefinition& task, TaskExecutionContext& context);
    std::string injectParameters(const std::string& text, const std::map<std::string, std::string>& params);
    nlohmann::json injectParametersIntoJson(const nlohmann::json& json, const std::map<std::string, std::string>& params);
    
    // File system operations
    void ensureDirectoryExists(const std::string& path);
    std::vector<std::string> listJsonFiles(const std::string& directory);
    bool isValidTaskFile(const std::string& filePath);
    
    // Task library management
    void createDefaultTasks();
    TaskDefinition createExampleTask();
};

} // namespace burwell

#endif // BURWELL_TASK_ENGINE_H