#include "ui_module.h"
#include "../common/structured_logger.h"
#include "../common/input_validator.h"
#include "../common/string_utils.h"
#include <iostream>
#include <algorithm>
#include <cctype>

using namespace burwell;

UIModule::UIModule() {
    SLOG_INFO().message("UIModule initialized");
}

std::string UIModule::getUserInput() {
    std::string input;
    std::cout << "> ";
    std::getline(std::cin, input);
    
    // Trim whitespace from input
    input = utils::StringUtils::trim(input);
    
    SLOG_DEBUG().message("User input received")
        .context("length", input.length());
    
    return input;
}

void UIModule::displayFeedback(const std::string& message) {
    // Validate message is not empty
    if (!burwell::InputValidator::isNotEmpty(message)) {
        SLOG_WARNING().message("Attempted to display empty feedback message");
        return;
    }
    
    // Validate message length
    const size_t MAX_MESSAGE_LENGTH = 10000;
    if (message.length() > MAX_MESSAGE_LENGTH) {
        SLOG_WARNING().message("Feedback message exceeds maximum length")
            .context("length", message.length())
            .context("max_length", MAX_MESSAGE_LENGTH);
        // Display truncated message
        std::cout << "[Agent]: " << message.substr(0, MAX_MESSAGE_LENGTH) << "... [truncated]" << std::endl;
        return;
    }
    
    std::cout << "[Agent]: " << message << std::endl;
}

void UIModule::displayLog(const std::string& logEntry) {
    // Validate log entry is not empty
    if (!burwell::InputValidator::isNotEmpty(logEntry)) {
        SLOG_WARNING().message("Attempted to display empty log entry");
        return;
    }
    
    // Validate log entry length
    const size_t MAX_LOG_LENGTH = 5000;
    if (logEntry.length() > MAX_LOG_LENGTH) {
        SLOG_WARNING().message("Log entry exceeds maximum length")
            .context("length", logEntry.length())
            .context("max_length", MAX_LOG_LENGTH);
        // Display truncated log
        std::cout << "[Log]: " << logEntry.substr(0, MAX_LOG_LENGTH) << "... [truncated]" << std::endl;
        return;
    }
    
    std::cout << "[Log]: " << logEntry << std::endl;
}

bool UIModule::promptUser(const std::string& question) {
    // Validate question is not empty
    if (!burwell::InputValidator::isNotEmpty(question)) {
        SLOG_ERROR().message("Empty question provided to promptUser");
        return false;
    }
    
    // Validate question length
    const size_t MAX_QUESTION_LENGTH = 1000;
    if (question.length() > MAX_QUESTION_LENGTH) {
        SLOG_WARNING().message("Question exceeds maximum length")
            .context("length", question.length())
            .context("max_length", MAX_QUESTION_LENGTH);
    }
    
    std::string response;
    int attempts = 0;
    const int MAX_ATTEMPTS = 10;
    
    while (attempts < MAX_ATTEMPTS) {
        std::cout << "[Agent]: " << question << " (yes/no): ";
        std::getline(std::cin, response);
        
        // Trim whitespace and convert to lowercase
        response = utils::StringUtils::trim(response);
        response = utils::StringUtils::toLowerCase(response);
        
        if (response == "yes" || response == "y") {
            SLOG_DEBUG().message("User responded yes to prompt")
                .context("question", question.substr(0, 50));
            return true;
        } else if (response == "no" || response == "n") {
            SLOG_DEBUG().message("User responded no to prompt")
                .context("question", question.substr(0, 50));
            return false;
        } else {
            std::cout << "Invalid response. Please type \'yes\' or \'no\'." << std::endl;
            attempts++;
        }
    }
    
    SLOG_ERROR().message("Max attempts exceeded in user prompt")
        .context("attempts", MAX_ATTEMPTS);
    return false;
}


