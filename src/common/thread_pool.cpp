#include "thread_pool.h"
#include "structured_logger.h"
#include <sstream>
#include <algorithm>

namespace burwell {

ThreadPool::ThreadPool(size_t numThreads) 
    : m_stopping(false)
    , m_numBusyThreads(0)
    , m_totalTasksExecuted(0)
    , m_totalTasksFailed(0)
    , m_totalExecutionTime(0) {
    
    if (numThreads == 0) {
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) {
            numThreads = 4; // Default fallback
        }
    }
    
    SLOG_INFO().message("Creating thread pool")
        .context("num_threads", numThreads);
    
    // Create worker threads
    for (size_t i = 0; i < numThreads; ++i) {
        m_threads.emplace_back(&ThreadPool::workerThread, this, i);
    }
}

ThreadPool::~ThreadPool() {
    shutdown(false);
}

void ThreadPool::workerThread(size_t threadId) {
    SLOG_DEBUG().message("Worker thread started")
        .context("thread_id", threadId);
    
    while (true) {
        Task task(Priority::LOW, nullptr);
        
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            
            m_condition.wait(lock, [this] {
                return m_stopping || !m_tasks.empty();
            });
            
            if (m_stopping && m_tasks.empty()) {
                break;
            }
            
            if (!m_tasks.empty()) {
                task = m_tasks.top();
                m_tasks.pop();
                m_numBusyThreads++;
            }
        }
        
        if (task.func) {
            auto startTime = std::chrono::steady_clock::now();
            
            try {
                task.func();
                m_totalTasksExecuted++;
            } catch (...) {
                m_totalTasksFailed++;
                
                if (m_exceptionHandler) {
                    m_exceptionHandler(std::current_exception());
                } else {
                    SLOG_ERROR().message("Uncaught exception in thread pool task")
                        .context("thread_id", threadId);
                }
            }
            
            auto endTime = std::chrono::steady_clock::now();
            auto duration = endTime - startTime;
            
            {
                std::lock_guard<std::mutex> lock(m_statsMutex);
                m_totalExecutionTime += duration;
            }
            
            m_numBusyThreads--;
            m_completionCondition.notify_all();
        }
    }
    
    // Only log in debug mode and catch any errors
    try {
        SLOG_DEBUG().message("Worker thread stopped")
            .context("thread_id", threadId);
    } catch (...) {
        // Ignore logging errors during shutdown
    }
}

void ThreadPool::waitForAll() {
    std::unique_lock<std::mutex> lock(m_queueMutex);
    
    m_completionCondition.wait(lock, [this] {
        return m_tasks.empty() && m_numBusyThreads == 0;
    });
}

void ThreadPool::shutdown(bool waitForTasks) {
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        
        if (m_stopping) {
            return; // Already shutting down
        }
        
        m_stopping = true;
        
        if (!waitForTasks) {
            // Clear pending tasks
            while (!m_tasks.empty()) {
                m_tasks.pop();
            }
        }
    }
    
    m_condition.notify_all();
    
    // Wait for all threads to finish
    for (std::thread& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    // Only log if we're not in destructor context (logger might be gone)
    // The destructor calls shutdown(false), so we can skip logging in that case
    if (waitForTasks) {
        try {
            SLOG_INFO().message("Thread pool shutdown complete")
                .context("tasks_executed", m_totalTasksExecuted.load())
                .context("tasks_failed", m_totalTasksFailed.load())
                .context("total_execution_time_ms", m_totalExecutionTime.count() / 1000000.0);
        } catch (...) {
            // Ignore logging errors during shutdown
        }
    }
}

size_t ThreadPool::getNumPendingTasks() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_tasks.size();
}

ThreadPool::PoolStats ThreadPool::getStats() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    
    PoolStats stats;
    stats.numThreads = m_threads.size();
    stats.numBusyThreads = m_numBusyThreads;
    stats.numIdleThreads = stats.numThreads - stats.numBusyThreads;
    stats.numPendingTasks = getNumPendingTasks();
    stats.totalTasksExecuted = m_totalTasksExecuted;
    stats.totalTasksFailed = m_totalTasksFailed;
    stats.totalExecutionTime = m_totalExecutionTime;
    
    if (stats.totalTasksExecuted > 0) {
        stats.averageTaskTime = stats.totalExecutionTime / stats.totalTasksExecuted;
    } else {
        stats.averageTaskTime = std::chrono::nanoseconds(0);
    }
    
    return stats;
}

void ThreadPool::setExceptionHandler(ExceptionHandler handler) {
    m_exceptionHandler = handler;
}

// AsyncTaskExecutor implementation

AsyncTaskExecutor::AsyncTaskExecutor(size_t numThreads) 
    : m_threadPool(numThreads)
    , m_taskCounter(0) {
}

AsyncTaskExecutor::~AsyncTaskExecutor() {
    // Cancel all active tasks
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        for (auto& pair : m_activeTasks) {
            pair.second->shouldStop = true;
        }
    }
    
    // Wait for tasks to complete
    m_threadPool.shutdown(true);
}

AsyncTaskExecutor::TaskId AsyncTaskExecutor::submitTask(TaskFunc task, ThreadPool::Priority priority) {
    TaskId taskId = generateTaskId();
    
    auto taskInfo = std::make_unique<TaskInfo>();
    taskInfo->shouldStop = false;
    taskInfo->startTime = std::chrono::steady_clock::now();
    
    auto& shouldStopRef = taskInfo->shouldStop;
    taskInfo->future = m_threadPool.submit(priority, 
        [task, &shouldStopRef, taskId, this]() {
            task(shouldStopRef);
            
            // Clean up completed task
            std::lock_guard<std::mutex> lock(m_tasksMutex);
            m_activeTasks.erase(taskId);
        });
    
    {
        std::lock_guard<std::mutex> lock(m_tasksMutex);
        m_activeTasks[taskId] = std::move(taskInfo);
    }
    
    // Clean up old completed tasks
    cleanupCompletedTasks();
    
    return taskId;
}

bool AsyncTaskExecutor::cancelTask(const TaskId& taskId) {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    
    auto it = m_activeTasks.find(taskId);
    if (it != m_activeTasks.end()) {
        it->second->shouldStop = true;
        return true;
    }
    
    return false;
}

bool AsyncTaskExecutor::isTaskRunning(const TaskId& taskId) const {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    
    auto it = m_activeTasks.find(taskId);
    if (it != m_activeTasks.end()) {
        auto status = it->second->future.wait_for(std::chrono::seconds(0));
        return status != std::future_status::ready;
    }
    
    return false;
}

bool AsyncTaskExecutor::waitForTask(const TaskId& taskId, int timeoutMs) {
    std::unique_lock<std::mutex> lock(m_tasksMutex);
    
    auto it = m_activeTasks.find(taskId);
    if (it == m_activeTasks.end()) {
        return false; // Task not found
    }
    
    auto& future = it->second->future;
    lock.unlock();
    
    if (timeoutMs < 0) {
        future.wait();
        return true;
    } else {
        auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));
        return status == std::future_status::ready;
    }
}

AsyncTaskExecutor::TaskId AsyncTaskExecutor::generateTaskId() {
    size_t id = m_taskCounter++;
    
    std::stringstream ss;
    ss << "TASK-" << std::hex << std::uppercase 
       << std::chrono::steady_clock::now().time_since_epoch().count()
       << "-" << id;
    
    return ss.str();
}

void AsyncTaskExecutor::cleanupCompletedTasks() {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    
    auto it = m_activeTasks.begin();
    while (it != m_activeTasks.end()) {
        auto status = it->second->future.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready) {
            it = m_activeTasks.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace burwell