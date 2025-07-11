#ifndef BURWELL_STRUCTURED_LOGGER_H
#define BURWELL_STRUCTURED_LOGGER_H

#include <string>
#include <memory>
#include <chrono>
#include <atomic>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>
#include "thread_safe_queue.h"

// LogLevel enum for structured logging
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR_LEVEL,  // Renamed to avoid Windows ERROR macro conflict
    CRITICAL
};

namespace burwell {

/**
 * @brief Log entry structure with structured data
 */
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string message;
    std::string logger_name;
    std::string file;
    int line;
    std::thread::id thread_id;
    nlohmann::json context;  // Additional structured data
    
    // Performance metrics
    std::chrono::nanoseconds duration;
    std::string operation_name;
    
    LogEntry() : line(0), duration(0) {}
};

/**
 * @brief Log formatter interface
 */
class ILogFormatter {
public:
    virtual ~ILogFormatter() = default;
    virtual std::string format(const LogEntry& entry) = 0;
};

/**
 * @brief JSON log formatter for structured logging
 */
class JsonLogFormatter : public ILogFormatter {
public:
    std::string format(const LogEntry& entry) override;
};

/**
 * @brief Human-readable log formatter
 */
class TextLogFormatter : public ILogFormatter {
public:
    std::string format(const LogEntry& entry) override;
};

/**
 * @brief Log sink interface
 */
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const LogEntry& entry) = 0;
    virtual void flush() = 0;
};

/**
 * @brief Console log sink
 */
class ConsoleLogSink : public ILogSink {
public:
    explicit ConsoleLogSink(std::shared_ptr<ILogFormatter> formatter);
    void write(const LogEntry& entry) override;
    void flush() override;
    
private:
    std::shared_ptr<ILogFormatter> m_formatter;
    std::mutex m_mutex;
};

/**
 * @brief File log sink with rotation support
 */
class RotatingFileLogSink : public ILogSink {
public:
    struct Config {
        std::string base_path;
        size_t max_file_size = 10 * 1024 * 1024;  // 10MB default
        size_t max_files = 5;                      // Keep 5 rotated files
        bool compress_rotated = false;             // Compress old logs
    };
    
    explicit RotatingFileLogSink(const Config& config, 
                                std::shared_ptr<ILogFormatter> formatter);
    ~RotatingFileLogSink();
    
    void write(const LogEntry& entry) override;
    void flush() override;
    
private:
    Config m_config;
    std::shared_ptr<ILogFormatter> m_formatter;
    std::unique_ptr<std::ofstream> m_file;
    std::mutex m_mutex;
    size_t m_current_size;
    
    void rotateIfNeeded();
    void openNewFile();
    void compressFile(const std::string& path);
    std::string generateFileName(int index = 0);
};

/**
 * @brief Performance metrics tracker
 */
class PerformanceTracker {
public:
    struct Metrics {
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> total_duration_ns{0};
        std::atomic<uint64_t> min_duration_ns{UINT64_MAX};
        std::atomic<uint64_t> max_duration_ns{0};
        std::atomic<uint64_t> errors{0};
        
        double getAverageDurationMs() const;
        nlohmann::json toJson() const;
    };
    
    struct MetricsSnapshot {
        uint64_t count = 0;
        uint64_t total_duration_ns = 0;
        uint64_t min_duration_ns = UINT64_MAX;
        uint64_t max_duration_ns = 0;
        uint64_t errors = 0;
        
        double getAverageDurationMs() const;
        nlohmann::json toJson() const;
    };
    
    void recordOperation(const std::string& operation, 
                        std::chrono::nanoseconds duration,
                        bool success = true);
    
    MetricsSnapshot getMetrics(const std::string& operation) const;
    std::unordered_map<std::string, MetricsSnapshot> getAllMetrics() const;
    void reset();
    
    // Periodic reporting
    void enablePeriodicReporting(std::chrono::seconds interval);
    void disablePeriodicReporting();
    
private:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, std::unique_ptr<Metrics>> m_metrics;
    std::atomic<bool> m_reporting_enabled{false};
    std::thread m_reporting_thread;
    
    void reportingLoop(std::chrono::seconds interval);
};

/**
 * @brief RAII performance timer
 */
class ScopedTimer {
public:
    ScopedTimer(const std::string& operation_name, 
                LogLevel level = LogLevel::DEBUG);
    ~ScopedTimer();
    
    // Disable copy
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    
    // Enable move
    ScopedTimer(ScopedTimer&& other) noexcept;
    ScopedTimer& operator=(ScopedTimer&& other) noexcept;
    
    void cancel() { m_cancelled = true; }
    
private:
    std::string m_operation_name;
    LogLevel m_level;
    std::chrono::high_resolution_clock::time_point m_start;
    bool m_cancelled;
};

/**
 * @brief Enhanced structured logger
 */
class StructuredLogger {
public:
    static StructuredLogger& getInstance();
    
    // Configuration
    void setLogLevel(LogLevel level);
    void addSink(std::shared_ptr<ILogSink> sink);
    void removeSink(std::shared_ptr<ILogSink> sink);
    void setAsyncLogging(bool async);
    
    // Logging methods
    void log(const LogEntry& entry);
    void log(LogLevel level, const std::string& message,
             const nlohmann::json& context = {});
    
    // Performance logging
    void logPerformance(const std::string& operation,
                       std::chrono::nanoseconds duration,
                       const nlohmann::json& context = {});
    
    // Structured logging helpers
    class LogBuilder {
    public:
        LogBuilder(StructuredLogger* logger, LogLevel level);
        
        LogBuilder& message(const std::string& msg);
        LogBuilder& context(const std::string& key, const nlohmann::json& value);
        LogBuilder& file(const char* file, int line);
        LogBuilder& operation(const std::string& op);
        LogBuilder& duration(std::chrono::nanoseconds ns);
        
        ~LogBuilder();  // Logs on destruction
        
    private:
        StructuredLogger* m_logger;
        LogEntry m_entry;
    };
    
    LogBuilder debug() { return LogBuilder(this, LogLevel::DEBUG); }
    LogBuilder info() { return LogBuilder(this, LogLevel::INFO); }
    LogBuilder warning() { return LogBuilder(this, LogLevel::WARNING); }
    LogBuilder error() { return LogBuilder(this, LogLevel::ERROR_LEVEL); }
    LogBuilder critical() { return LogBuilder(this, LogLevel::CRITICAL); }
    
    // Performance tracking
    PerformanceTracker& getPerformanceTracker() { return m_performance_tracker; }
    
    // Shutdown and flush
    void shutdown();
    void flush();
    
private:
    StructuredLogger();
    ~StructuredLogger();
    
    LogLevel m_min_level;
    std::vector<std::shared_ptr<ILogSink>> m_sinks;
    std::mutex m_config_mutex;
    
    // Async logging
    bool m_async_enabled;
    ThreadSafeQueue<LogEntry> m_log_queue;
    std::thread m_logging_thread;
    std::atomic<bool> m_shutdown{false};
    
    // Performance tracking
    PerformanceTracker m_performance_tracker;
    
    void asyncLoggingLoop();
    void processLogEntry(const LogEntry& entry);
};

// Convenience macros for structured logging
#define SLOG_DEBUG() burwell::StructuredLogger::getInstance().debug().file(__FILE__, __LINE__)
#define SLOG_INFO() burwell::StructuredLogger::getInstance().info().file(__FILE__, __LINE__)
#define SLOG_WARNING() burwell::StructuredLogger::getInstance().warning().file(__FILE__, __LINE__)
#define SLOG_ERROR() burwell::StructuredLogger::getInstance().error().file(__FILE__, __LINE__)
#define SLOG_CRITICAL() burwell::StructuredLogger::getInstance().critical().file(__FILE__, __LINE__)

// Performance timing macros
#define SCOPED_TIMER(operation) burwell::ScopedTimer _timer(operation)
#define SCOPED_TIMER_LEVEL(operation, level) burwell::ScopedTimer _timer(operation, level)

} // namespace burwell

#endif // BURWELL_STRUCTURED_LOGGER_H