#ifndef BURWELL_ERROR_HANDLER_H
#define BURWELL_ERROR_HANDLER_H

#include <string>
#include <exception>
#include <functional>
#include <map>
#include <vector>
#include <chrono>

enum class ErrorType {
    LLM_CONNECTION_ERROR,
    LLM_API_ERROR,
    LLM_RATE_LIMIT_ERROR,
    OS_OPERATION_ERROR,
    OS_PERMISSION_ERROR,
    TASK_EXECUTION_ERROR,
    TASK_NOT_FOUND_ERROR,
    CONFIGURATION_ERROR,
    VALIDATION_ERROR,
    SECURITY_ERROR,
    TIMEOUT_ERROR,
    UNKNOWN_ERROR
};

enum class ErrorSeverity {
    LOW,
    MEDIUM,
    HIGH,
    CRITICAL
};

struct ErrorInfo {
    ErrorType type;
    ErrorSeverity severity;
    std::string message;
    std::string details;
    std::string context;
    std::chrono::system_clock::time_point timestamp;
    
    ErrorInfo(ErrorType t, ErrorSeverity s, const std::string& msg, 
              const std::string& det = "", const std::string& ctx = "")
        : type(t), severity(s), message(msg), details(det), context(ctx),
          timestamp(std::chrono::system_clock::now()) {}
};

struct RecoveryOptions {
    std::vector<std::string> availableActions;
    std::function<bool()> retryFunction;
    std::function<void()> rollbackFunction;
    std::function<void()> skipFunction;
    bool canRetry = false;
    bool canRollback = false;
    bool canSkip = false;
};

class BurwellException : public std::exception {
public:
    BurwellException(const ErrorInfo& error) : m_errorInfo(error) {}
    
    const char* what() const noexcept override {
        return m_errorInfo.message.c_str();
    }
    
    const ErrorInfo& getErrorInfo() const { return m_errorInfo; }
    
private:
    ErrorInfo m_errorInfo;
};

class ErrorHandler {
public:
    static ErrorHandler& getInstance();
    
    // Error handling methods
    void handleError(const ErrorInfo& error);
    void handleException(const std::exception& e, const std::string& context = "");
    
    // Recovery methods
    void setRecoveryOptions(ErrorType type, const RecoveryOptions& options);
    bool attemptRecovery(const ErrorInfo& error);
    
    // Error logging and tracking
    void logError(const ErrorInfo& error);
    std::vector<ErrorInfo> getRecentErrors(size_t count = 10);
    void clearErrorHistory();
    
    // User interaction
    using UserPromptCallback = std::function<std::string(const std::string& message, const std::vector<std::string>& options)>;
    void setUserPromptCallback(UserPromptCallback callback);
    
    // Configuration
    void setMaxRetries(ErrorType type, int maxRetries);
    void setRetryDelay(ErrorType type, int delayMs);
    
private:
    ErrorHandler() = default;
    ~ErrorHandler() = default;
    ErrorHandler(const ErrorHandler&) = delete;
    ErrorHandler& operator=(const ErrorHandler&) = delete;
    
    std::vector<ErrorInfo> m_errorHistory;
    std::map<ErrorType, RecoveryOptions> m_recoveryOptions;
    std::map<ErrorType, int> m_maxRetries;
    std::map<ErrorType, int> m_retryDelays;
    std::map<ErrorType, int> m_retryCount;
    UserPromptCallback m_userPromptCallback;
    
    bool shouldRetry(const ErrorInfo& error);
    void resetRetryCount(ErrorType type);
    std::string errorTypeToString(ErrorType type);
    std::string errorSeverityToString(ErrorSeverity severity);
};

// Convenience macros for error handling
#define BURWELL_THROW(type, severity, message, details, context) \
    throw BurwellException(ErrorInfo(type, severity, message, details, context))

#define BURWELL_HANDLE_ERROR(type, severity, message, details, context) \
    ErrorHandler::getInstance().handleError(ErrorInfo(type, severity, message, details, context))

#define BURWELL_TRY_CATCH(code, context) \
    try { \
        code; \
    } catch (const std::exception& e) { \
        ErrorHandler::getInstance().handleException(e, context); \
    }

#endif // BURWELL_ERROR_HANDLER_H