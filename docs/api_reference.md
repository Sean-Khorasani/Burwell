# Burwell API Reference

**Platform Note**: This API reference documents the Windows implementation of Burwell. Cross-platform abstractions are planned for future releases to support Linux and macOS.

## Table of Contents
1. [Core Interfaces](#core-interfaces)
2. [Orchestrator API](#orchestrator-api)
3. [State Management API](#state-management-api)
4. [Task Engine API](#task-engine-api)
5. [OCAL API](#ocal-api)
6. [Utility APIs](#utility-apis)

## Core Interfaces

### IStateManager

Base interface for state management implementations.

```cpp
class IStateManager {
public:
    // Request Management
    virtual std::string createRequest(const std::string& userInput) = 0;
    virtual bool hasRequest(const std::string& requestId) const = 0;
    virtual void removeRequest(const std::string& requestId) = 0;
    
    // Execution Context
    virtual ExecutionContext& getExecutionContext(const std::string& requestId) = 0;
    virtual void markExecutionActive(const std::string& requestId) = 0;
    virtual void markExecutionComplete(const std::string& requestId, 
                                     const TaskExecutionResult& result) = 0;
    
    // Variables
    virtual void setVariable(const std::string& requestId, 
                           const std::string& name, 
                           const nlohmann::json& value) = 0;
    virtual nlohmann::json getVariable(const std::string& requestId, 
                                     const std::string& name) const = 0;
};
```

### DIContainer

Dependency injection container for service management.

```cpp
class DIContainer {
public:
    enum class Lifetime {
        SINGLETON,   // Single instance for entire application
        TRANSIENT    // New instance for each request
    };
    
    // Register service with implementation
    template<typename TInterface, typename TImplementation>
    void registerService(Lifetime lifetime = Lifetime::SINGLETON);
    
    // Register factory function
    template<typename TInterface>
    void registerFactory(std::function<std::shared_ptr<TInterface>()> factory,
                        Lifetime lifetime = Lifetime::SINGLETON);
    
    // Resolve service
    template<typename TInterface>
    std::shared_ptr<TInterface> resolve();
};
```

## Orchestrator API

### OrchestratorFacade

Main entry point for orchestration operations.

```cpp
class OrchestratorFacade {
public:
    // Initialize the orchestrator
    bool initialize();
    
    // Process user request
    TaskExecutionResult processRequest(const std::string& userInput);
    
    // Execute command plan
    TaskExecutionResult executePlan(const nlohmann::json& plan,
                                  ExecutionContext& context);
    
    // Event handling
    void registerEventHandler(OrchestratorEvent event,
                            std::function<void(const EventData&)> handler);
    
    // Shutdown
    void shutdown();
};
```

### ExecutionEngine

Handles command execution and resource management.

```cpp
class ExecutionEngine {
public:
    // Execute single command
    ExecutionResult executeCommand(const Command& command,
                                 ExecutionContext& context);
    
    // Execute command sequence
    ExecutionResult executeSequence(const std::vector<Command>& commands,
                                  ExecutionContext& context);
    
    // Resource management
    void allocateResources(const ResourceRequirements& requirements);
    void releaseResources();
    
    // Error recovery
    void setErrorRecoveryStrategy(std::unique_ptr<IErrorRecovery> strategy);
};
```

### EventManager

Event-driven communication system.

```cpp
class EventManager {
public:
    // Subscribe to events
    using EventHandler = std::function<void(const Event&)>;
    SubscriptionId subscribe(EventType type, EventHandler handler);
    void unsubscribe(SubscriptionId id);
    
    // Publish events
    void publish(const Event& event);
    void publishAsync(const Event& event);
    
    // Event filtering
    void setEventFilter(std::function<bool(const Event&)> filter);
};
```

## State Management API

### StateManager

Thread-safe state management implementation.

```cpp
class StateManager {
public:
    // Configuration
    void setMaxCompletedExecutions(size_t maxExecutions);
    void setMaxActivityLogSize(size_t maxSize);
    
    // Request lifecycle
    std::string createRequest(const std::string& userInput);
    void markExecutionActive(const std::string& requestId);
    void markExecutionComplete(const std::string& requestId,
                             const TaskExecutionResult& result);
    
    // Variable management
    void setVariable(const std::string& requestId,
                   const std::string& name,
                   const nlohmann::json& value);
    nlohmann::json getVariable(const std::string& requestId,
                             const std::string& name) const;
    void inheritVariables(const std::string& fromRequestId,
                        const std::string& toRequestId);
    
    // Script management
    void pushScript(const std::string& requestId,
                   const std::string& scriptPath);
    void popScript(const std::string& requestId);
    bool isScriptInStack(const std::string& requestId,
                        const std::string& scriptPath) const;
    
    // Activity logging
    void logActivity(const std::string& activity);
    std::vector<std::string> getRecentActivity() const;
    
    // State persistence
    nlohmann::json exportState() const;
    void importState(const nlohmann::json& state);
};
```

### ExecutionContext

Execution context for request processing.

```cpp
struct ExecutionContext {
    std::string requestId;
    std::string originalRequest;
    nlohmann::json currentEnvironment;
    std::vector<std::string> executionLog;
    bool requiresUserConfirmation;
    std::map<std::string, nlohmann::json> variables;
    
    // Nested script support
    int nestingLevel;
    int maxNestingLevel;
    std::vector<std::string> scriptStack;
    std::map<std::string, nlohmann::json> subScriptResults;
};
```

## Task Engine API

### TaskEngine

Manages task definitions and execution.

```cpp
class TaskEngine {
public:
    // Task registration
    void registerTask(const std::string& name,
                     std::unique_ptr<ITask> task);
    
    // Task execution
    TaskResult executeTask(const std::string& taskName,
                         const TaskParameters& params);
    
    // Task discovery
    std::vector<std::string> getAvailableTasks() const;
    TaskDefinition getTaskDefinition(const std::string& taskName) const;
    
    // Batch operations
    std::vector<TaskResult> executeBatch(const std::vector<TaskRequest>& requests);
};
```

### ITask Interface

Base interface for task implementations.

```cpp
class ITask {
public:
    virtual ~ITask() = default;
    
    // Task metadata
    virtual std::string getName() const = 0;
    virtual std::string getDescription() const = 0;
    virtual TaskParameters getRequiredParameters() const = 0;
    
    // Execution
    virtual TaskResult execute(const TaskParameters& params,
                             ExecutionContext& context) = 0;
    
    // Validation
    virtual bool validate(const TaskParameters& params) const = 0;
};
```

## OCAL API

### OCAL (OS Control Abstraction Layer)

Windows OS operations abstraction. Currently uses Windows API (USER32, KERNEL32). Future versions will provide cross-platform abstractions for Linux and macOS.

```cpp
class OCAL {
public:
    // Window management
    std::vector<WindowInfo> enumerateWindows();
    bool focusWindow(HWND window);
    bool closeWindow(HWND window);
    WindowRect getWindowRect(HWND window);
    
    // Keyboard operations
    void sendKeyPress(int virtualKey);
    void sendKeyRelease(int virtualKey);
    void typeText(const std::string& text);
    void sendHotkey(const std::vector<int>& keys);
    
    // Mouse operations
    void moveMouse(int x, int y);
    void mouseClick(MouseButton button = MouseButton::LEFT);
    void mouseDrag(int startX, int startY, int endX, int endY);
    
    // Process management
    ProcessId launchApplication(const std::string& path,
                               const std::string& args = "");
    bool terminateProcess(ProcessId pid);
    std::vector<ProcessInfo> listProcesses();
    
    // File operations
    bool fileExists(const std::string& path);
    bool createDirectory(const std::string& path);
    bool deleteFile(const std::string& path);
    std::vector<std::string> listDirectory(const std::string& path);
    
    // Registry operations (Windows)
    bool readRegistryValue(const std::string& key,
                          const std::string& valueName,
                          std::string& result);
    bool writeRegistryValue(const std::string& key,
                           const std::string& valueName,
                           const std::string& value);
};
```

## Utility APIs

### ThreadPool

High-performance thread pool with priority support.

```cpp
class ThreadPool {
public:
    enum class Priority {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        CRITICAL = 3
    };
    
    // Submit task
    template<typename F, typename... Args>
    auto submit(Priority priority, F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;
    
    // Pool management
    void waitForAll();
    void shutdown(bool waitForTasks = true);
    
    // Statistics
    struct PoolStats {
        size_t numThreads;
        size_t numPendingTasks;
        size_t totalTasksExecuted;
    };
    PoolStats getStats() const;
};
```

### InputValidator

Comprehensive input validation utilities.

```cpp
class InputValidator {
public:
    struct ValidationResult {
        bool isValid;
        std::string errorMessage;
        std::string sanitizedValue;
    };
    
    // Path validation
    static ValidationResult validateFilePath(const std::string& path);
    static ValidationResult validateDirectoryPath(const std::string& path);
    
    // Command validation
    static ValidationResult validateCommand(const std::string& command);
    static ValidationResult validateScriptPath(const std::string& path);
    
    // Security checks
    static bool containsPathTraversal(const std::string& path);
    static bool containsShellMetacharacters(const std::string& input);
    
    // Sanitization
    static std::string sanitizeForShell(const std::string& input);
    static std::string sanitizeForRegistry(const std::string& input);
};
```

### ResourceMonitor

Resource usage tracking and leak detection.

```cpp
class ResourceMonitor {
public:
    enum class ResourceType {
        MEMORY,
        HANDLE,
        THREAD,
        FILE
    };
    
    // Tracking
    void trackAllocation(ResourceType type, void* address, size_t size);
    void trackDeallocation(void* address);
    
    // Reporting
    std::vector<ResourceInfo> detectLeaks() const;
    ResourceStats getStatistics() const;
    void printReport() const;
    
    // Configuration
    void setLeakDetectionEnabled(bool enabled);
    void setDetailedTracking(bool detailed);
};
```

### RAII Wrappers

Automatic resource management wrappers.

```cpp
// Window handle wrapper
class WindowHandle {
public:
    explicit WindowHandle(HANDLE handle);
    ~WindowHandle();
    
    HANDLE get() const noexcept;
    bool isValid() const noexcept;
    HANDLE release() noexcept;
    void reset(HANDLE handle = nullptr);
};

// File handle wrapper
class FileHandle {
public:
    explicit FileHandle(HANDLE handle);
    ~FileHandle();
    
    HANDLE get() const noexcept;
    bool isValid() const noexcept;
    
    // File operations
    bool read(void* buffer, DWORD size, DWORD* bytesRead);
    bool write(const void* buffer, DWORD size, DWORD* bytesWritten);
};

// Registry key wrapper
class RegistryHandle {
public:
    explicit RegistryHandle(HKEY key);
    ~RegistryHandle();
    
    HKEY get() const noexcept;
    bool isValid() const noexcept;
    
    // Registry operations
    bool getValue(const std::string& name, std::string& value);
    bool setValue(const std::string& name, const std::string& value);
};
```

### ServiceFactory

Factory for creating and configuring services.

```cpp
class ServiceFactory {
public:
    // Initialize all services
    void initializeServices(const std::string& configPath);
    
    // Service creation
    std::shared_ptr<CommandParser> createCommandParser();
    std::shared_ptr<LLMConnector> createLLMConnector(const std::string& provider);
    std::shared_ptr<TaskEngine> createTaskEngine();
    std::shared_ptr<OCAL> createOCAL();
    std::shared_ptr<StateManager> createStateManager(bool threadSafe = true);
    
    // Access DI container
    DIContainer& getContainer();
    
    // Custom service registration
    template<typename TInterface>
    void registerCreator(const std::string& name,
                        std::function<std::shared_ptr<TInterface>()> creator);
};
```

## Error Handling

### Exception Hierarchy

```cpp
// Base exception
class BurwellException : public std::exception {
public:
    explicit BurwellException(const std::string& message,
                               int errorCode = 0);
    const char* what() const noexcept override;
    int getErrorCode() const noexcept;
};

// Specific exceptions
class ConfigException : public BurwellException { };
class ValidationException : public BurwellException { };
class ExecutionException : public BurwellException { };
class ResourceException : public BurwellException { };
class NetworkException : public BurwellException { };
```

### Error Codes

```cpp
enum class ErrorCode {
    SUCCESS = 0,
    
    // Configuration errors (1000-1999)
    CONFIG_FILE_NOT_FOUND = 1001,
    CONFIG_PARSE_ERROR = 1002,
    CONFIG_VALIDATION_ERROR = 1003,
    
    // Validation errors (2000-2999)
    INVALID_INPUT = 2001,
    PATH_TRAVERSAL_DETECTED = 2002,
    COMMAND_INJECTION_DETECTED = 2003,
    
    // Execution errors (3000-3999)
    COMMAND_NOT_FOUND = 3001,
    EXECUTION_TIMEOUT = 3002,
    RESOURCE_UNAVAILABLE = 3003,
    
    // System errors (4000-4999)
    MEMORY_ALLOCATION_FAILED = 4001,
    THREAD_CREATION_FAILED = 4002,
    FILE_ACCESS_DENIED = 4003
};
```

## Usage Examples

### Basic Request Processing

```cpp
// Initialize system
ServiceFactory factory;
factory.initializeServices("config/burwell.json");

auto orchestrator = factory.getContainer().resolve<OrchestratorFacade>();
orchestrator->initialize();

// Process user request
auto result = orchestrator->processRequest("Open Notepad and type Hello World");

if (result.success) {
    std::cout << "Task completed successfully\n";
} else {
    std::cerr << "Task failed: " << result.errorMessage << "\n";
}
```

### Custom Task Implementation

```cpp
class CustomTask : public ITask {
public:
    std::string getName() const override {
        return "CustomTask";
    }
    
    TaskResult execute(const TaskParameters& params,
                      ExecutionContext& context) override {
        // Task implementation
        auto value = params.get<std::string>("input");
        
        // Process value
        std::string result = processValue(value);
        
        // Store result
        context.variables["result"] = result;
        
        return TaskResult::success(result);
    }
};

// Register task
taskEngine->registerTask("custom", std::make_unique<CustomTask>());
```

### Event-Driven Processing

```cpp
// Subscribe to events
auto eventManager = factory.getContainer().resolve<EventManager>();

auto subscriptionId = eventManager->subscribe(
    EventType::TASK_COMPLETED,
    [](const Event& event) {
        std::cout << "Task completed: " << event.data["taskName"] << "\n";
    }
);

// Publish event
Event completionEvent{
    EventType::TASK_COMPLETED,
    {{"taskName", "OpenNotepad"}, {"duration", 1500}}
};
eventManager->publishAsync(completionEvent);
```