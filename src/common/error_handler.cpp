#include "error_handler.h"
#include "structured_logger.h"
#include <thread>
#include <chrono>
#include <sstream>

ErrorHandler& ErrorHandler::getInstance() {
    static ErrorHandler instance;
    return instance;
}

void ErrorHandler::handleError(const ErrorInfo& error) {
    logError(error);
    
    // Attempt automatic recovery if available
    if (attemptRecovery(error)) {
        SLOG_INFO().message("Error recovered automatically").context("error_message", error.message);
        return;
    }
    
    // For critical errors, notify user immediately
    if (error.severity == ErrorSeverity::CRITICAL) {
        if (m_userPromptCallback) {
            std::vector<std::string> options = {"Abort", "Continue"};
            auto recoveryIter = m_recoveryOptions.find(error.type);
            if (recoveryIter != m_recoveryOptions.end()) {
                const auto& recovery = recoveryIter->second;
                if (recovery.canRetry) options.push_back("Retry");
                if (recovery.canRollback) options.push_back("Rollback");
                if (recovery.canSkip) options.push_back("Skip");
            }
            
            std::string userChoice = m_userPromptCallback(
                "Critical Error: " + error.message + "\nDetails: " + error.details,
                options
            );
            
            // Handle user choice
            if (userChoice == "Retry" && recoveryIter != m_recoveryOptions.end() && recoveryIter->second.canRetry) {
                if (recoveryIter->second.retryFunction && recoveryIter->second.retryFunction()) {
                    resetRetryCount(error.type);
                    return;
                }
            } else if (userChoice == "Rollback" && recoveryIter != m_recoveryOptions.end() && recoveryIter->second.canRollback) {
                if (recoveryIter->second.rollbackFunction) {
                    recoveryIter->second.rollbackFunction();
                    return;
                }
            } else if (userChoice == "Skip" && recoveryIter != m_recoveryOptions.end() && recoveryIter->second.canSkip) {
                if (recoveryIter->second.skipFunction) {
                    recoveryIter->second.skipFunction();
                    return;
                }
            }
        }
        
        // If no recovery possible, throw exception
        throw BurwellException(error);
    }
}

void ErrorHandler::handleException(const std::exception& e, const std::string& context) {
    const BurwellException* cogniError = dynamic_cast<const BurwellException*>(&e);
    if (cogniError) {
        handleError(cogniError->getErrorInfo());
    } else {
        ErrorInfo error(ErrorType::UNKNOWN_ERROR, ErrorSeverity::HIGH, 
                       e.what(), "", context);
        handleError(error);
    }
}

void ErrorHandler::setRecoveryOptions(ErrorType type, const RecoveryOptions& options) {
    m_recoveryOptions[type] = options;
}

bool ErrorHandler::attemptRecovery(const ErrorInfo& error) {
    auto recoveryIter = m_recoveryOptions.find(error.type);
    if (recoveryIter == m_recoveryOptions.end()) {
        return false;
    }
    
    const auto& recovery = recoveryIter->second;
    
    // Check if we should retry
    if (recovery.canRetry && shouldRetry(error)) {
        SLOG_INFO().message("Attempting automatic retry").context("error_message", error.message);
        
        // Add delay before retry
        auto delayIter = m_retryDelays.find(error.type);
        if (delayIter != m_retryDelays.end()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayIter->second));
        }
        
        m_retryCount[error.type]++;
        
        if (recovery.retryFunction && recovery.retryFunction()) {
            resetRetryCount(error.type);
            return true;
        }
    }
    
    return false;
}

void ErrorHandler::logError(const ErrorInfo& error) {
    // Add to error history
    m_errorHistory.push_back(error);
    
    // Keep only recent errors (max 1000)
    if (m_errorHistory.size() > 1000) {
        m_errorHistory.erase(m_errorHistory.begin());
    }
    
    // Log to system logger
    
    std::ostringstream logMessage;
    logMessage << "[" << errorTypeToString(error.type) << "] " 
               << error.message;
    if (!error.details.empty()) {
        logMessage << " - Details: " << error.details;
    }
    if (!error.context.empty()) {
        logMessage << " - Context: " << error.context;
    }
    
    // Use structured logging with proper level
    switch (error.severity) {
        case ErrorSeverity::LOW: 
            SLOG_DEBUG().message(logMessage.str()).context("error_type", errorTypeToString(error.type)).context("severity", errorSeverityToString(error.severity));
            break;
        case ErrorSeverity::MEDIUM: 
            SLOG_WARNING().message(logMessage.str()).context("error_type", errorTypeToString(error.type)).context("severity", errorSeverityToString(error.severity));
            break;
        case ErrorSeverity::HIGH: 
            SLOG_ERROR().message(logMessage.str()).context("error_type", errorTypeToString(error.type)).context("severity", errorSeverityToString(error.severity));
            break;
        case ErrorSeverity::CRITICAL: 
            SLOG_CRITICAL().message(logMessage.str()).context("error_type", errorTypeToString(error.type)).context("severity", errorSeverityToString(error.severity));
            break;
        default: 
            SLOG_INFO().message(logMessage.str()).context("error_type", errorTypeToString(error.type)).context("severity", errorSeverityToString(error.severity));
            break;
    }
}

std::vector<ErrorInfo> ErrorHandler::getRecentErrors(size_t count) {
    size_t start = (m_errorHistory.size() > count) ? m_errorHistory.size() - count : 0;
    return std::vector<ErrorInfo>(m_errorHistory.begin() + start, m_errorHistory.end());
}

void ErrorHandler::clearErrorHistory() {
    m_errorHistory.clear();
    m_retryCount.clear();
}

void ErrorHandler::setUserPromptCallback(UserPromptCallback callback) {
    m_userPromptCallback = callback;
}

void ErrorHandler::setMaxRetries(ErrorType type, int maxRetries) {
    m_maxRetries[type] = maxRetries;
}

void ErrorHandler::setRetryDelay(ErrorType type, int delayMs) {
    m_retryDelays[type] = delayMs;
}

bool ErrorHandler::shouldRetry(const ErrorInfo& error) {
    auto maxRetriesIter = m_maxRetries.find(error.type);
    if (maxRetriesIter == m_maxRetries.end()) {
        return false; // No retry limit set, don't retry
    }
    
    int currentRetries = m_retryCount[error.type];
    return currentRetries < maxRetriesIter->second;
}

void ErrorHandler::resetRetryCount(ErrorType type) {
    m_retryCount[type] = 0;
}

std::string ErrorHandler::errorTypeToString(ErrorType type) {
    switch (type) {
        case ErrorType::LLM_CONNECTION_ERROR: return "LLM_CONNECTION";
        case ErrorType::LLM_API_ERROR: return "LLM_API";
        case ErrorType::LLM_RATE_LIMIT_ERROR: return "LLM_RATE_LIMIT";
        case ErrorType::OS_OPERATION_ERROR: return "OS_OPERATION";
        case ErrorType::OS_PERMISSION_ERROR: return "OS_PERMISSION";
        case ErrorType::TASK_EXECUTION_ERROR: return "TASK_EXECUTION";
        case ErrorType::TASK_NOT_FOUND_ERROR: return "TASK_NOT_FOUND";
        case ErrorType::CONFIGURATION_ERROR: return "CONFIGURATION";
        case ErrorType::VALIDATION_ERROR: return "VALIDATION";
        case ErrorType::SECURITY_ERROR: return "SECURITY";
        case ErrorType::TIMEOUT_ERROR: return "TIMEOUT";
        case ErrorType::UNKNOWN_ERROR: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

std::string ErrorHandler::errorSeverityToString(ErrorSeverity severity) {
    switch (severity) {
        case ErrorSeverity::LOW: return "LOW";
        case ErrorSeverity::MEDIUM: return "MEDIUM";
        case ErrorSeverity::HIGH: return "HIGH";
        case ErrorSeverity::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}