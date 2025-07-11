#include "structured_logger.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace burwell {

namespace fs = std::filesystem;

// Utility functions
namespace {
    std::string formatTimestamp(const std::chrono::system_clock::time_point& tp) {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }
    
    std::string logLevelToString(::LogLevel level) {
        switch (level) {
            case ::LogLevel::DEBUG: return "DEBUG";
            case ::LogLevel::INFO: return "INFO";
            case ::LogLevel::WARNING: return "WARNING";
            case ::LogLevel::ERROR_LEVEL: return "ERROR";
            case ::LogLevel::CRITICAL: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }
    
    std::string threadIdToString(std::thread::id id) {
        std::stringstream ss;
        ss << id;
        return ss.str();
    }
}

// JsonLogFormatter implementation
std::string JsonLogFormatter::format(const LogEntry& entry) {
    nlohmann::json log_json;
    
    log_json["timestamp"] = formatTimestamp(entry.timestamp);
    log_json["level"] = logLevelToString(entry.level);
    log_json["message"] = entry.message;
    log_json["logger"] = entry.logger_name;
    log_json["thread"] = threadIdToString(entry.thread_id);
    
    if (!entry.file.empty()) {
        log_json["source"]["file"] = entry.file;
        log_json["source"]["line"] = entry.line;
    }
    
    if (!entry.operation_name.empty()) {
        log_json["operation"] = entry.operation_name;
        log_json["duration_ms"] = entry.duration.count() / 1000000.0;
    }
    
    if (!entry.context.empty()) {
        log_json["context"] = entry.context;
    }
    
    return log_json.dump() + "\n";
}

// TextLogFormatter implementation
std::string TextLogFormatter::format(const LogEntry& entry) {
    std::stringstream ss;
    
    // Timestamp and level
    ss << "[" << formatTimestamp(entry.timestamp) << "] ";
    ss << "[" << std::setw(8) << logLevelToString(entry.level) << "] ";
    
    // Thread ID (shortened)
    std::string thread_str = threadIdToString(entry.thread_id);
    if (thread_str.length() > 6) {
        thread_str = thread_str.substr(thread_str.length() - 6);
    }
    ss << "[" << std::setw(6) << thread_str << "] ";
    
    // Logger name
    if (!entry.logger_name.empty()) {
        ss << "[" << entry.logger_name << "] ";
    }
    
    // Message
    ss << entry.message;
    
    // Source location for errors and above
    if (!entry.file.empty() && (entry.level >= ::LogLevel::ERROR_LEVEL)) {
        fs::path file_path(entry.file);
        ss << " (" << file_path.filename().string() << ":" << entry.line << ")";
    }
    
    // Performance metrics
    if (!entry.operation_name.empty()) {
        ss << " [" << entry.operation_name << ": " 
           << std::fixed << std::setprecision(2) 
           << (entry.duration.count() / 1000000.0) << "ms]";
    }
    
    // Context (compact format)
    if (!entry.context.empty()) {
        ss << " " << entry.context.dump();
    }
    
    ss << "\n";
    return ss.str();
}

// ConsoleLogSink implementation
ConsoleLogSink::ConsoleLogSink(std::shared_ptr<ILogFormatter> formatter)
    : m_formatter(formatter) {}

void ConsoleLogSink::write(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::string formatted = m_formatter->format(entry);
    
    // Use different streams and colors based on level
    if (entry.level >= ::LogLevel::ERROR_LEVEL) {
        #ifdef _WIN32
        HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        #endif
        
        std::cerr << formatted;
        
        #ifdef _WIN32
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        #endif
    } else {
        std::cout << formatted;
    }
}

void ConsoleLogSink::flush() {
    std::cout.flush();
    std::cerr.flush();
}

// RotatingFileLogSink implementation
RotatingFileLogSink::RotatingFileLogSink(const Config& config,
                                       std::shared_ptr<ILogFormatter> formatter)
    : m_config(config), m_formatter(formatter), m_current_size(0) {
    openNewFile();
}

RotatingFileLogSink::~RotatingFileLogSink() {
    if (m_file && m_file->is_open()) {
        m_file->close();
    }
}

void RotatingFileLogSink::write(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_file || !m_file->is_open()) {
        openNewFile();
    }
    
    std::string formatted = m_formatter->format(entry);
    *m_file << formatted;
    m_current_size += formatted.size();
    
    rotateIfNeeded();
}

void RotatingFileLogSink::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file && m_file->is_open()) {
        m_file->flush();
    }
}

void RotatingFileLogSink::rotateIfNeeded() {
    if (m_current_size >= m_config.max_file_size) {
        m_file->close();
        
        // Rotate existing files
        for (int i = static_cast<int>(m_config.max_files) - 1; i >= 0; --i) {
            std::string old_name = generateFileName(i);
            std::string new_name = generateFileName(i + 1);
            
            if (fs::exists(old_name)) {
                if (i == static_cast<int>(m_config.max_files) - 1) {
                    fs::remove(old_name);  // Remove oldest
                } else {
                    fs::rename(old_name, new_name);
                }
            }
        }
        
        // Rename current file
        fs::rename(m_config.base_path, generateFileName(0));
        
        // Compress if needed
        if (m_config.compress_rotated) {
            compressFile(generateFileName(0));
        }
        
        // Open new file
        openNewFile();
    }
}

void RotatingFileLogSink::openNewFile() {
    m_file = std::make_unique<std::ofstream>(m_config.base_path, std::ios::app);
    m_current_size = fs::exists(m_config.base_path) ? fs::file_size(m_config.base_path) : 0;
}

void RotatingFileLogSink::compressFile(const std::string& path) {
    // Note: In production, use a proper compression library like zlib
    // For now, just rename to .gz to indicate it should be compressed
    fs::rename(path, path + ".gz");
}

std::string RotatingFileLogSink::generateFileName(int index) {
    if (index == 0) {
        return m_config.base_path;
    }
    
    fs::path p(m_config.base_path);
    std::string stem = p.stem().string();
    std::string ext = p.extension().string();
    
    return (p.parent_path() / (stem + "." + std::to_string(index) + ext)).string();
}

// PerformanceTracker implementation
double PerformanceTracker::Metrics::getAverageDurationMs() const {
    uint64_t count_val = count.load();
    if (count_val == 0) return 0.0;
    return (total_duration_ns.load() / count_val) / 1000000.0;
}

nlohmann::json PerformanceTracker::Metrics::toJson() const {
    nlohmann::json j;
    j["count"] = count.load();
    j["errors"] = errors.load();
    j["average_ms"] = getAverageDurationMs();
    j["min_ms"] = min_duration_ns.load() / 1000000.0;
    j["max_ms"] = max_duration_ns.load() / 1000000.0;
    j["total_ms"] = total_duration_ns.load() / 1000000.0;
    return j;
}

double PerformanceTracker::MetricsSnapshot::getAverageDurationMs() const {
    if (count == 0) return 0.0;
    return (total_duration_ns / count) / 1000000.0;
}

nlohmann::json PerformanceTracker::MetricsSnapshot::toJson() const {
    nlohmann::json j;
    j["count"] = count;
    j["errors"] = errors;
    j["average_ms"] = getAverageDurationMs();
    j["min_ms"] = min_duration_ns / 1000000.0;
    j["max_ms"] = max_duration_ns / 1000000.0;
    j["total_ms"] = total_duration_ns / 1000000.0;
    return j;
}

void PerformanceTracker::recordOperation(const std::string& operation,
                                       std::chrono::nanoseconds duration,
                                       bool success) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    auto& metrics = m_metrics[operation];
    if (!metrics) {
        metrics = std::make_unique<Metrics>();
    }
    
    lock.unlock();  // Release lock for atomic operations
    
    metrics->count++;
    metrics->total_duration_ns += duration.count();
    
    // Update min/max
    uint64_t dur = duration.count();
    uint64_t current_min = metrics->min_duration_ns.load();
    while (dur < current_min && 
           !metrics->min_duration_ns.compare_exchange_weak(current_min, dur)) {}
    
    uint64_t current_max = metrics->max_duration_ns.load();
    while (dur > current_max && 
           !metrics->max_duration_ns.compare_exchange_weak(current_max, dur)) {}
    
    if (!success) {
        metrics->errors++;
    }
}

PerformanceTracker::MetricsSnapshot PerformanceTracker::getMetrics(const std::string& operation) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    auto it = m_metrics.find(operation);
    if (it != m_metrics.end() && it->second) {
        // Create a snapshot of the atomic values
        MetricsSnapshot result;
        result.count = it->second->count.load();
        result.total_duration_ns = it->second->total_duration_ns.load();
        result.min_duration_ns = it->second->min_duration_ns.load();
        result.max_duration_ns = it->second->max_duration_ns.load();
        result.errors = it->second->errors.load();
        return result;
    }
    
    return MetricsSnapshot{};
}

std::unordered_map<std::string, PerformanceTracker::MetricsSnapshot> 
PerformanceTracker::getAllMetrics() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    std::unordered_map<std::string, MetricsSnapshot> result;
    for (const auto& [op, metrics] : m_metrics) {
        if (metrics) {
            // Create a snapshot of the atomic values
            MetricsSnapshot m;
            m.count = metrics->count.load();
            m.total_duration_ns = metrics->total_duration_ns.load();
            m.min_duration_ns = metrics->min_duration_ns.load();
            m.max_duration_ns = metrics->max_duration_ns.load();
            m.errors = metrics->errors.load();
            result[op] = m;
        }
    }
    
    return result;
}

void PerformanceTracker::reset() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_metrics.clear();
}

void PerformanceTracker::enablePeriodicReporting(std::chrono::seconds interval) {
    bool expected = false;
    if (m_reporting_enabled.compare_exchange_strong(expected, true)) {
        m_reporting_thread = std::thread(&PerformanceTracker::reportingLoop, this, interval);
    }
}

void PerformanceTracker::disablePeriodicReporting() {
    m_reporting_enabled = false;
    if (m_reporting_thread.joinable()) {
        // Wake up the thread if it's sleeping by using a shorter timeout
        // The thread checks m_reporting_enabled so it will exit
        m_reporting_thread.join();
    }
}

void PerformanceTracker::reportingLoop(std::chrono::seconds interval) {
    auto next_report_time = std::chrono::steady_clock::now() + interval;
    
    while (m_reporting_enabled) {
        // Check every 100ms if we should stop
        auto now = std::chrono::steady_clock::now();
        if (now >= next_report_time) {
            // Time to generate a report
            if (!m_reporting_enabled) break;
            
            auto metrics = getAllMetrics();
            nlohmann::json report;
            report["timestamp"] = formatTimestamp(std::chrono::system_clock::now());
            report["metrics"] = nlohmann::json::object();
            
            for (const auto& [op, m] : metrics) {
                report["metrics"][op] = m.toJson();
            }
            
            // Log the performance report (catch exceptions to avoid crashes)
            try {
                StructuredLogger::getInstance().info()
                    .message("Performance Report")
                    .context("report", report);
            } catch (...) {
                // Ignore logging errors
            }
            
            next_report_time += interval;
        } else {
            // Sleep for a short time to allow quick shutdown
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// ScopedTimer implementation
ScopedTimer::ScopedTimer(const std::string& operation_name, ::LogLevel level)
    : m_operation_name(operation_name)
    , m_level(level)
    , m_start(std::chrono::high_resolution_clock::now())
    , m_cancelled(false) {}

ScopedTimer::~ScopedTimer() {
    if (!m_cancelled && !m_operation_name.empty()) {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_start);
        
        StructuredLogger::getInstance().logPerformance(m_operation_name, duration);
    }
}

ScopedTimer::ScopedTimer(ScopedTimer&& other) noexcept
    : m_operation_name(std::move(other.m_operation_name))
    , m_level(other.m_level)
    , m_start(other.m_start)
    , m_cancelled(other.m_cancelled) {
    other.m_cancelled = true;
}

ScopedTimer& ScopedTimer::operator=(ScopedTimer&& other) noexcept {
    if (this != &other) {
        m_operation_name = std::move(other.m_operation_name);
        m_level = other.m_level;
        m_start = other.m_start;
        m_cancelled = other.m_cancelled;
        other.m_cancelled = true;
    }
    return *this;
}

// StructuredLogger implementation
StructuredLogger& StructuredLogger::getInstance() {
    static StructuredLogger instance;
    return instance;
}

StructuredLogger::StructuredLogger() 
    : m_min_level(::LogLevel::INFO)
    , m_async_enabled(false) {
    
    // Add default console sink
    auto formatter = std::make_shared<TextLogFormatter>();
    addSink(std::make_shared<ConsoleLogSink>(formatter));
}

StructuredLogger::~StructuredLogger() {
    shutdown();
}

void StructuredLogger::setLogLevel(::LogLevel level) {
    std::lock_guard<std::mutex> lock(m_config_mutex);
    m_min_level = level;
}

void StructuredLogger::addSink(std::shared_ptr<ILogSink> sink) {
    std::lock_guard<std::mutex> lock(m_config_mutex);
    m_sinks.push_back(sink);
}

void StructuredLogger::removeSink(std::shared_ptr<ILogSink> sink) {
    std::lock_guard<std::mutex> lock(m_config_mutex);
    m_sinks.erase(std::remove(m_sinks.begin(), m_sinks.end(), sink), m_sinks.end());
}

void StructuredLogger::setAsyncLogging(bool async) {
    if (m_async_enabled == async) return;
    
    if (async) {
        m_async_enabled = true;
        m_logging_thread = std::thread(&StructuredLogger::asyncLoggingLoop, this);
    } else {
        m_async_enabled = false;
        m_shutdown = true;
        if (m_logging_thread.joinable()) {
            m_logging_thread.join();
        }
        m_shutdown = false;
    }
}

void StructuredLogger::log(const LogEntry& entry) {
    // Don't accept new log entries if we're shutting down
    if (m_shutdown) return;
    
    if (entry.level < m_min_level) return;
    
    if (m_async_enabled && !m_shutdown) {
        // Only push if the queue accepts it (not closed)
        m_log_queue.push(entry);
    } else if (!m_shutdown) {
        processLogEntry(entry);
    }
}

void StructuredLogger::log(::LogLevel level, const std::string& message,
                          const nlohmann::json& context) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.message = message;
    entry.thread_id = std::this_thread::get_id();
    entry.context = context;
    
    log(entry);
}

void StructuredLogger::logPerformance(const std::string& operation,
                                     std::chrono::nanoseconds duration,
                                     const nlohmann::json& context) {
    // Record in performance tracker
    m_performance_tracker.recordOperation(operation, duration, true);
    
    // Log if duration exceeds threshold (e.g., 100ms)
    if (duration > std::chrono::milliseconds(100)) {
        LogEntry entry;
        entry.timestamp = std::chrono::system_clock::now();
        entry.level = ::LogLevel::WARNING;
        entry.message = "Slow operation detected";
        entry.operation_name = operation;
        entry.duration = duration;
        entry.thread_id = std::this_thread::get_id();
        entry.context = context;
        
        log(entry);
    }
}

void StructuredLogger::shutdown() {
    // First stop performance reporting to prevent new log entries
    m_performance_tracker.disablePeriodicReporting();
    
    // Signal shutdown
    m_shutdown = true;
    
    // If async logging is enabled, stop the thread properly
    if (m_async_enabled) {
        // Don't close the queue immediately - let it drain first
        // Give the logging thread time to process remaining items
        if (m_logging_thread.joinable()) {
            // Wait up to 2 seconds for the thread to finish
            for (int i = 0; i < 20 && !m_log_queue.empty(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Now close the queue to unblock the thread if it's waiting
            m_log_queue.close();
            
            // Join the thread
            m_logging_thread.join();
        }
        
        m_async_enabled = false;
    }
    
    // Flush all sinks one last time
    try {
        std::lock_guard<std::mutex> lock(m_config_mutex);
        for (auto& sink : m_sinks) {
            sink->flush();
        }
    } catch (...) {
        // Ignore errors during shutdown
    }
}

void StructuredLogger::flush() {
    // If async logging is enabled, wait for queue to be empty
    if (m_async_enabled) {
        // Process any remaining items in the queue
        while (!m_log_queue.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    // Flush all sinks
    std::lock_guard<std::mutex> lock(m_config_mutex);
    for (auto& sink : m_sinks) {
        sink->flush();
    }
}

void StructuredLogger::asyncLoggingLoop() {
    while (!m_shutdown) {
        auto entry_opt = m_log_queue.popWithTimeout(100);
        if (entry_opt) {
            processLogEntry(*entry_opt);
        }
    }
    
    // Process remaining entries
    while (auto entry_opt = m_log_queue.tryPop()) {
        processLogEntry(*entry_opt);
    }
}

void StructuredLogger::processLogEntry(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(m_config_mutex);
    for (auto& sink : m_sinks) {
        sink->write(entry);
    }
}

// LogBuilder implementation
StructuredLogger::LogBuilder::LogBuilder(StructuredLogger* logger, ::LogLevel level)
    : m_logger(logger) {
    m_entry.level = level;
    m_entry.timestamp = std::chrono::system_clock::now();
    m_entry.thread_id = std::this_thread::get_id();
}

StructuredLogger::LogBuilder& 
StructuredLogger::LogBuilder::message(const std::string& msg) {
    m_entry.message = msg;
    return *this;
}

StructuredLogger::LogBuilder& 
StructuredLogger::LogBuilder::context(const std::string& key, const nlohmann::json& value) {
    m_entry.context[key] = value;
    return *this;
}

StructuredLogger::LogBuilder& 
StructuredLogger::LogBuilder::file(const char* file, int line) {
    m_entry.file = file;
    m_entry.line = line;
    return *this;
}

StructuredLogger::LogBuilder& 
StructuredLogger::LogBuilder::operation(const std::string& op) {
    m_entry.operation_name = op;
    return *this;
}

StructuredLogger::LogBuilder& 
StructuredLogger::LogBuilder::duration(std::chrono::nanoseconds ns) {
    m_entry.duration = ns;
    return *this;
}

StructuredLogger::LogBuilder::~LogBuilder() {
    if (m_logger) {
        m_logger->log(m_entry);
    }
}

} // namespace burwell