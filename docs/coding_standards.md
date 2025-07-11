# Burwell Coding Standards

## Table of Contents
1. [General Principles](#general-principles)
2. [File Organization](#file-organization)
3. [Naming Conventions](#naming-conventions)
4. [Code Layout](#code-layout)
5. [Language Features](#language-features)
6. [Error Handling](#error-handling)
7. [Documentation](#documentation)
8. [Testing](#testing)
9. [Performance](#performance)
10. [Security](#security)

## General Principles

### Core Values
1. **Readability** - Code is written once but read many times
2. **Maintainability** - Easy to modify and extend
3. **Reliability** - Predictable behavior with proper error handling
4. **Performance** - Efficient but not at the cost of clarity
5. **Security** - Secure by default, validate all inputs

### The Boy Scout Rule
Always leave the code better than you found it. If you touch a file, improve it.

## File Organization

### Directory Structure
```
burwell/
├── src/                    # Source files
│   ├── common/            # Shared utilities
│   ├── orchestrator/      # Orchestration components
│   ├── task_engine/       # Task management
│   ├── llm_connector/     # LLM integration
│   ├── ocal/             # OS abstraction
│   └── ui_module/        # User interface
├── include/               # Public headers (if needed)
├── tests/                 # Unit tests
├── config/               # Configuration files
├── scripts/              # Build and utility scripts
└── docs/                 # Documentation
```

### File Naming
- Use snake_case for file names: `thread_pool.cpp`, `input_validator.h`
- One class per file, named after the class
- Implementation files use `.cpp`, headers use `.h`
- Keep related files together in modules

### File Structure
```cpp
// Header file structure
#ifndef COGNIFORGE_MODULE_CLASS_NAME_H
#define COGNIFORGE_MODULE_CLASS_NAME_H

// System headers
#include <memory>
#include <string>

// Project headers
#include "common/types.h"

// Forward declarations
namespace burwell {
class ForwardDeclared;

// Class definition
class ClassName {
    // ... 
};

} // namespace burwell

#endif // COGNIFORGE_MODULE_CLASS_NAME_H
```

## Naming Conventions

### General Rules
- Use descriptive names that convey intent
- Avoid abbreviations except for well-known ones (e.g., `ctx` for context)
- Be consistent within a module

### Specific Conventions

#### Classes and Structs
```cpp
class ThreadPool;        // PascalCase
struct ExecutionContext; // PascalCase
```

#### Functions and Methods
```cpp
void processRequest();   // camelCase
bool isValid() const;    // camelCase, const-correct
```

#### Variables
```cpp
int requestCount;        // camelCase for locals
m_memberVariable;        // m_ prefix for members
s_staticVariable;        // s_ prefix for statics
g_globalVariable;        // g_ prefix for globals (avoid!)
```

#### Constants and Enums
```cpp
constexpr int MAX_RETRIES = 3;     // UPPER_SNAKE_CASE
enum class LogLevel {
    DEBUG,                          // UPPER_SNAKE_CASE
    INFO,
    WARNING,
    ERROR_LEVEL                     // Suffix if keyword conflict
};
```

#### Namespaces
```cpp
namespace burwell {              // lowercase
namespace detail {                  // implementation details
} // namespace detail
} // namespace burwell
```

#### Template Parameters
```cpp
template<typename T>                // T for single type
template<typename TKey, typename TValue>  // T prefix for multiple
template<size_t N>                  // N for numeric
```

## Code Layout

### Indentation and Spacing
- Use 4 spaces for indentation (no tabs)
- Maximum line length: 100 characters
- Use blank lines to separate logical sections

### Braces
```cpp
// K&R style for functions and classes
class Example {
public:
    void method() {
        if (condition) {
            // code
        } else {
            // code
        }
    }
};

// Single line allowed for simple cases
int getValue() const { return m_value; }
```

### Class Layout
```cpp
class Example {
public:
    // Type definitions
    using CallbackType = std::function<void()>;
    
    // Constructors/Destructor
    Example();
    ~Example();
    
    // Public methods
    void publicMethod();
    
protected:
    // Protected methods
    virtual void protectedMethod();
    
private:
    // Private methods
    void privateMethod();
    
    // Member variables (at bottom)
    int m_value;
    std::string m_name;
};
```

## Language Features

### Modern C++ Usage
- Prefer C++17 features where they improve code
- Use `auto` for complex types, explicit for simple ones
- Prefer range-based for loops
- Use structured bindings for multiple returns

```cpp
// Good
auto result = complexFunction();
for (const auto& item : container) {
    processItem(item);
}

// Structured binding
auto [success, value] = tryGetValue();
```

### Smart Pointers
```cpp
// Prefer unique_ptr for single ownership
std::unique_ptr<Resource> resource = std::make_unique<Resource>();

// Use shared_ptr only when sharing is necessary
std::shared_ptr<SharedResource> shared = std::make_shared<SharedResource>();

// Never use raw pointers for ownership
Resource* raw = new Resource();  // BAD!
```

### Const Correctness
```cpp
// Mark methods const if they don't modify state
int getValue() const { return m_value; }

// Use const references for parameters
void processData(const std::string& data);

// Mark variables const when possible
const int maxRetries = getMaxRetries();
```

### RAII (Resource Acquisition Is Initialization)
```cpp
// Good - automatic cleanup
{
    WindowHandle window(::FindWindow(nullptr, "App"));
    // Use window
} // Automatic cleanup

// Bad - manual cleanup required
HWND hwnd = ::FindWindow(nullptr, "App");
// Use hwnd
::CloseHandle(hwnd); // Easy to forget!
```

## Error Handling

### Exception Usage
```cpp
// Throw specific exceptions with context
throw ValidationException("Invalid file path: " + path, 
                         ErrorCode::INVALID_PATH);

// Catch specific exceptions
try {
    operation();
} catch (const ValidationException& e) {
    Logger::log(LogLevel::ERROR_LEVEL, e.what());
    // Handle validation error
} catch (const std::exception& e) {
    // Handle other errors
}
```

### Error Codes
```cpp
// Use enum class for type safety
enum class ErrorCode {
    SUCCESS = 0,
    FILE_NOT_FOUND = 1001,
    ACCESS_DENIED = 1002
};

// Return optional for fallible operations
std::optional<std::string> tryReadFile(const std::string& path);
```

### Assertions
```cpp
// Use assertions for programming errors
assert(pointer != nullptr);  // Debug only

// Use exceptions for runtime errors
if (!isValid(input)) {
    throw std::invalid_argument("Invalid input");
}
```

## Documentation

### File Headers
```cpp
/**
 * @file thread_pool.h
 * @brief High-performance thread pool implementation
 * @author Your Name
 * @date 2024-01-01
 */
```

### Class Documentation
```cpp
/**
 * @class ThreadPool
 * @brief Manages a pool of worker threads for concurrent execution
 * 
 * This class provides a high-performance thread pool with priority
 * support and graceful shutdown capabilities.
 * 
 * @see AsyncTaskExecutor for cancellable task support
 */
class ThreadPool {
```

### Method Documentation
```cpp
/**
 * @brief Submit a task for execution
 * @param priority Task priority level
 * @param f Function to execute
 * @param args Function arguments
 * @return Future for the task result
 * @throws std::runtime_error if pool is shutting down
 */
template<typename F, typename... Args>
auto submit(Priority priority, F&& f, Args&&... args);
```

### Inline Comments
```cpp
// Explain why, not what
int retries = 3;  // AWS recommends 3 retries for transient errors

// TODO: Implement caching for performance
// FIXME: Handle edge case when list is empty
// NOTE: This algorithm is O(n log n)
```

## Testing

### Unit Test Structure
```cpp
// test_component_name.cpp
#include <gtest/gtest.h>
#include "component.h"

class ComponentTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Test setup
    }
    
    void TearDown() override {
        // Cleanup
    }
};

TEST_F(ComponentTest, MethodShouldReturnExpectedValue) {
    // Arrange
    Component component;
    
    // Act
    auto result = component.method();
    
    // Assert
    EXPECT_EQ(result, expectedValue);
}
```

### Test Naming
- Test class: `ComponentNameTest`
- Test method: `MethodName_StateUnderTest_ExpectedBehavior`

### Coverage Requirements
- Minimum 80% line coverage
- 100% coverage for critical paths
- Test error conditions and edge cases

## Performance

### Guidelines
1. **Measure First** - Profile before optimizing
2. **Algorithmic Efficiency** - Choose appropriate data structures
3. **Avoid Premature Optimization** - Clarity first, performance second
4. **Cache Wisely** - Cache expensive operations when beneficial

### Best Practices
```cpp
// Pass by const reference for large objects
void processData(const LargeObject& obj);

// Use move semantics
std::vector<Data> data = std::move(tempData);

// Reserve container capacity
std::vector<int> numbers;
numbers.reserve(1000);  // If size is known

// Prefer algorithms over hand-written loops
std::sort(data.begin(), data.end());
```

## Security

### Input Validation
```cpp
// Always validate external input
auto result = InputValidator::validateFilePath(userPath);
if (!result.isValid) {
    throw ValidationException(result.errorMessage);
}

// Sanitize for specific contexts
std::string safe = InputValidator::sanitizeForShell(userInput);
```

### Secure Practices
1. **Never hardcode credentials** - Use CredentialManager
2. **Validate all paths** - Prevent directory traversal
3. **Sanitize commands** - Prevent injection attacks
4. **Use secure random** - For any security-sensitive randomness
5. **Principle of least privilege** - Request minimum permissions

### Common Vulnerabilities to Avoid
```cpp
// BAD - Buffer overflow risk
char buffer[256];
strcpy(buffer, userInput);  // NO!

// GOOD - Safe string handling
std::string buffer = userInput;

// BAD - Command injection
system(("command " + userInput).c_str());  // NO!

// GOOD - Validated and escaped
auto validated = validateCommand(userInput);
executeSecureCommand(validated);
```

## Code Review Checklist

Before submitting code for review, ensure:

- [ ] Code compiles without warnings
- [ ] All tests pass
- [ ] Code follows naming conventions
- [ ] Complex logic is documented
- [ ] No hardcoded values (use constants/config)
- [ ] Error cases are handled
- [ ] Resources are properly managed (RAII)
- [ ] Input validation is present
- [ ] No commented-out code
- [ ] No TODO items (or they're tracked)

## Enforcement

These standards are enforced through:
1. Compiler flags: `-Wall -Wextra -Werror`
2. Static analysis: clang-tidy with project configuration
3. Code reviews: All changes require peer review
4. Automated formatting: clang-format with project style

Remember: Consistency is key. When in doubt, follow the existing pattern in the codebase.