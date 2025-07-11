# Burwell Developer Guide

## Table of Contents
1. [Introduction](#introduction)
2. [Architecture Overview](#architecture-overview)
3. [Getting Started](#getting-started)
4. [Core Components](#core-components)
5. [Development Workflow](#development-workflow)
6. [Best Practices](#best-practices)
7. [Testing](#testing)
8. [Troubleshooting](#troubleshooting)

## Introduction

Burwell is an enterprise-grade desktop automation agent built with C++17. This guide provides comprehensive information for developers working on or extending the Burwell platform.

### Design Philosophy
- **Learn, Abstract, Reuse**: The system learns from user requests, abstracts solutions, and reuses them
- **Security First**: All inputs validated, credentials encrypted, resources managed safely
- **Testability**: Components designed for unit testing with dependency injection
- **Performance**: Optimized for concurrent operations with thread pools and async execution

## Architecture Overview

### High-Level Architecture
```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│    UI Module    │────▶│   Orchestrator   │────▶│  Task Engine    │
└─────────────────┘     │     Facade       │     └─────────────────┘
                        └──────────────────┘
                                 │
                    ┌────────────┴────────────┐
                    ▼                         ▼
           ┌──────────────────┐     ┌──────────────────┐
           │ Execution Engine │     │  State Manager   │
           └──────────────────┘     └──────────────────┘
                    │                         │
                    ▼                         ▼
           ┌──────────────────┐     ┌──────────────────┐
           │      OCAL        │     │ Event Manager    │
           └──────────────────┘     └──────────────────┘
```

### Component Responsibilities

#### Orchestrator Facade
- Provides unified interface to all subsystems
- Coordinates component interactions
- Maintains backward compatibility

#### Execution Engine
- Executes commands and tasks
- Manages resource allocation
- Handles error recovery

#### State Manager
- Tracks execution contexts
- Manages variables and results
- Provides activity logging

#### Event Manager
- Implements event-driven architecture
- Manages observer pattern
- Handles async notifications

#### OCAL (OS Control Abstraction Layer)
- Abstracts OS-specific operations
- Provides uniform interface for system control
- Handles Windows API calls

## Getting Started

### Prerequisites
- Windows 10/11 (primary target)
- MSYS2 MinGW-w64 toolchain
- CMake 3.10 or higher
- C++17 compatible compiler
- nlohmann/json library

### Building the Project

#### Linux Cross-Compilation
```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-mingw.cmake
cmake --build .
```

#### Windows Native Build
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Running Tests
```bash
./build/bin/test_threading.exe
./build/bin/test_dependency_injection.exe
```

## Core Components

### 1. Dependency Injection

The project uses a custom DI container for managing dependencies:

```cpp
#include "common/dependency_injection.h"

// Register a service
ServiceLocator::registerService<IMessageService, ConsoleMessageService>();

// Resolve a service
auto service = ServiceLocator::resolve<IMessageService>();
```

### 2. Thread Pool

High-performance thread pool with priority support:

```cpp
#include "common/thread_pool.h"

ThreadPool pool(4); // 4 worker threads

// Submit task with priority
auto future = pool.submit(ThreadPool::Priority::HIGH, []() {
    // Task implementation
});

// Wait for result
future.wait();
```

### 3. State Management

Thread-safe state management with reader-writer locks:

```cpp
#include "orchestrator/state_manager_thread_safe.h"

StateManager stateManager;
std::string requestId = stateManager.createRequest("User input");

// Set variables
stateManager.setVariable(requestId, "result", jsonValue);

// Get execution context
auto& context = stateManager.getExecutionContext(requestId);
```

### 4. RAII Wrappers

Automatic resource management:

```cpp
#include "common/raii_wrappers.h"

{
    WindowHandle window(::FindWindow(nullptr, "Notepad"));
    if (window.isValid()) {
        // Use window handle
    }
    // Automatic cleanup on scope exit
}
```

### 5. Input Validation

Comprehensive input validation:

```cpp
#include "common/input_validator.h"

auto result = InputValidator::validateFilePath(userPath);
if (!result.isValid) {
    Logger::log(LogLevel::ERROR_LEVEL, result.errorMessage);
    return;
}
```

## Development Workflow

### 1. Creating New Components

Use the service factory pattern:

```cpp
class INewService {
public:
    virtual ~INewService() = default;
    virtual void doWork() = 0;
};

class NewServiceImpl : public INewService {
public:
    void doWork() override {
        // Implementation
    }
};

// Register in ServiceFactory::initializeServices()
m_container.registerService<INewService, NewServiceImpl>();
```

### 2. Adding New Commands

1. Define command in `config/cpl/commands.json`
2. Implement handler in appropriate module
3. Register with command parser
4. Add tests

### 3. Error Handling

Use custom exceptions:

```cpp
#include "common/exceptions.h"

void riskyOperation() {
    if (errorCondition) {
        throw ExecutionException("Operation failed", ERROR_CODE);
    }
}

try {
    riskyOperation();
} catch (const ExecutionException& e) {
    Logger::log(LogLevel::ERROR_LEVEL, e.what());
    // Handle error
}
```

## Best Practices

### 1. Memory Management
- Always use smart pointers for ownership
- Prefer `unique_ptr` for single ownership
- Use `shared_ptr` only when sharing is necessary
- Implement RAII for all resources

### 2. Thread Safety
- Use `const` methods for read-only operations
- Prefer reader-writer locks for data structures
- Minimize lock scope
- Avoid nested locks to prevent deadlocks

### 3. Error Handling
- Throw exceptions for exceptional conditions
- Use error codes for expected failures
- Always provide context in error messages
- Log errors at appropriate levels

### 4. Code Organization
- Keep files under 500 lines
- One class per file
- Group related functionality in namespaces
- Use forward declarations to reduce coupling

### 5. Performance
- Profile before optimizing
- Use thread pools for concurrent tasks
- Implement caching where appropriate
- Avoid premature optimization

## Testing

### Unit Testing

Create tests for new components:

```cpp
#include <cassert>
#include "your_component.h"

void testYourComponent() {
    YourComponent component;
    
    // Test setup
    component.initialize();
    
    // Test operation
    auto result = component.performOperation();
    
    // Verify
    assert(result.isSuccess());
    assert(result.getValue() == expectedValue);
}
```

### Integration Testing

Test component interactions:

```cpp
void testComponentIntegration() {
    ServiceFactory factory;
    factory.initializeServices("test_config.json");
    
    auto service1 = factory.getContainer().resolve<IService1>();
    auto service2 = factory.getContainer().resolve<IService2>();
    
    // Test interaction
    service1->sendData(testData);
    auto result = service2->receiveData();
    
    assert(result == testData);
}
```

### Compilation Testing

Always verify cross-compilation:

```bash
x86_64-w64-mingw32-g++ -std=c++17 -Wall -Wextra -Werror \
    -I src -I include -c your_file.cpp -o test.o
```

## Troubleshooting

### Common Issues

#### 1. Compilation Errors
- **Issue**: Undefined references
- **Solution**: Check CMakeLists.txt for missing source files
- **Prevention**: Always update CMakeLists.txt when adding files

#### 2. Runtime Crashes
- **Issue**: Access violations
- **Solution**: Enable debug builds and use debugger
- **Prevention**: Use RAII and validate all inputs

#### 3. Deadlocks
- **Issue**: Application hangs
- **Solution**: Review lock ordering, use lock-free structures
- **Prevention**: Follow locking hierarchy, avoid nested locks

#### 4. Memory Leaks
- **Issue**: Increasing memory usage
- **Solution**: Use ResourceMonitor in debug builds
- **Prevention**: Always use smart pointers

### Debug Techniques

1. **Enable Debug Logging**
```cpp
Logger::setLogLevel(LogLevel::DEBUG);
```

2. **Use Resource Monitor**
```cpp
#ifdef DEBUG
ResourceMonitor::getInstance().printReport();
#endif
```

3. **Thread Debugging**
```cpp
// Name threads for easier debugging
std::thread worker([&]() {
    SetThreadDescription(GetCurrentThread(), L"WorkerThread");
    // Thread work
});
```

## Contributing

### Code Review Checklist
- [ ] All inputs validated
- [ ] Resources use RAII
- [ ] Thread-safe where necessary
- [ ] Appropriate error handling
- [ ] Unit tests included
- [ ] Documentation updated
- [ ] No compiler warnings

### Style Guidelines
- Use descriptive variable names
- Keep functions focused and small
- Document complex algorithms
- Use const correctness
- Follow existing patterns

## Appendix

### Useful Resources
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- [Windows API Documentation](https://docs.microsoft.com/en-us/windows/win32/)
- [CMake Documentation](https://cmake.org/documentation/)

### Performance Profiling
- Use Windows Performance Toolkit
- Enable timing in debug builds
- Monitor thread pool statistics
- Track memory allocations

### Security Considerations
- Never store plain text credentials
- Validate all file paths
- Sanitize command inputs
- Use secure random number generation
- Follow principle of least privilege