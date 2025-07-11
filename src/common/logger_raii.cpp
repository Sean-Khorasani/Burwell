#include "structured_logger.h"
#include "raii_wrappers.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <memory>

using namespace burwell::raii;

// Static member initialization
LogLevel Logger::s_currentLogLevel = LogLevel::INFO;
std::mutex Logger::s_logMutex;
std::string Logger::s_logFilePath;
bool Logger::s_fileLoggingEnabled = false;
std::vector<LogLevel> Logger::s_sourceLocationLevels = {LogLevel::ERROR_LEVEL, LogLevel::CRITICAL};

// Thread-local storage for log file handle
thread_local std::unique_ptr<FileWrapper> t_logFile;

void Logger::log(LogLevel level, const std::string& message, const char* file, int line) {
    if (level < s_currentLogLevel) {
        return;
    }

    std::lock_guard<std::mutex> lock(s_logMutex);
    
    // Create timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm = *std::localtime(&time_t);
    
    // Build log message
    std::ostringstream oss;
    oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << "." << std::setfill('0') << std::setw(3) << ms.count() << "]"
        << " [" << logLevelToString(level) << "] ";
    
    // Add file and line info for error levels
    if (file && line > 0 && shouldShowSourceLocation(level)) {
        oss << "[" << extractFileName(file) << ":" << line << "] ";
    }
    
    oss << message;
    
    std::string fullMessage = oss.str();
    
    // Output to console
    if (level >= LogLevel::ERROR_LEVEL) {
        std::cerr << fullMessage << std::endl;
    } else {
        std::cout << fullMessage << std::endl;
    }
    
    // Output to file if enabled
    if (s_fileLoggingEnabled && !s_logFilePath.empty()) {
        try {
            // Use RAII wrapper for automatic file closing
            if (!t_logFile || !t_logFile->isValid()) {
                t_logFile = std::make_unique<FileWrapper>(s_logFilePath, "a");
            }
            
            if (t_logFile->isValid()) {
                std::string logLine = fullMessage + "\n";
                t_logFile->write(logLine.c_str(), 1, logLine.size());
                t_logFile->flush();
            }
        } catch (const std::exception& e) {
            std::cerr << "[LOGGER ERROR] Failed to write to log file: " << e.what() << std::endl;
        }
    }
}

void Logger::setLogLevel(LogLevel level) {
    s_currentLogLevel = level;
}

void Logger::setLogLevel(const std::string& levelStr) {
    s_currentLogLevel = stringToLogLevel(levelStr);
}

void Logger::setLogFile(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    s_logFilePath = filePath;
    
    // Close current file if open
    if (t_logFile) {
        t_logFile.reset();
    }
    
    // Create directory if it doesn't exist
    if (!filePath.empty()) {
        std::filesystem::path path(filePath);
        std::filesystem::path dir = path.parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            try {
                std::filesystem::create_directories(dir);
            } catch (const std::exception& e) {
                std::cerr << "[LOGGER ERROR] Failed to create log directory: " << e.what() << std::endl;
            }
        }
    }
}

void Logger::enableFileLogging(bool enable) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    s_fileLoggingEnabled = enable;
    
    // Close file if disabling
    if (!enable && t_logFile) {
        t_logFile.reset();
    }
}

void Logger::setSourceLocationLevels(const std::string& levels) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    s_sourceLocationLevels.clear();
    
    std::istringstream iss(levels);
    std::string level;
    while (std::getline(iss, level, ',')) {
        // Trim whitespace
        level.erase(0, level.find_first_not_of(" \t"));
        level.erase(level.find_last_not_of(" \t") + 1);
        
        s_sourceLocationLevels.push_back(stringToLogLevel(level));
    }
}

std::string Logger::logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARNING:  return "WARNING";
        case LogLevel::ERROR_LEVEL:    return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}

LogLevel Logger::stringToLogLevel(const std::string& levelStr) {
    std::string upper = levelStr;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper == "DEBUG") return LogLevel::DEBUG;
    if (upper == "INFO") return LogLevel::INFO;
    if (upper == "WARNING" || upper == "WARN") return LogLevel::WARNING;
    if (upper == "ERROR") return LogLevel::ERROR_LEVEL;
    if (upper == "CRITICAL" || upper == "FATAL") return LogLevel::CRITICAL;
    
    return LogLevel::INFO; // Default
}

std::string Logger::extractFileName(const char* filePath) {
    std::string path(filePath);
    size_t lastSlash = path.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        return path.substr(lastSlash + 1);
    }
    return path;
}

bool Logger::shouldShowSourceLocation(LogLevel level) {
    return std::find(s_sourceLocationLevels.begin(), 
                     s_sourceLocationLevels.end(), 
                     level) != s_sourceLocationLevels.end();
}