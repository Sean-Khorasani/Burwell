#ifndef BURWELL_THREAD_POOL_H
#define BURWELL_THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <map>

namespace burwell {

/**
 * @brief Thread pool for executing tasks concurrently
 * 
 * Features:
 * - Task priority support
 * - Graceful shutdown
 * - Thread pool monitoring
 * - Exception handling
 */
class ThreadPool {
public:
    /**
     * @brief Task priority levels
     */
    enum class Priority {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        CRITICAL = 3
    };
    
    /**
     * @brief Thread pool statistics
     */
    struct PoolStats {
        size_t numThreads;
        size_t numIdleThreads;
        size_t numBusyThreads;
        size_t numPendingTasks;
        size_t totalTasksExecuted;
        size_t totalTasksFailed;
        std::chrono::nanoseconds totalExecutionTime;
        std::chrono::nanoseconds averageTaskTime;
    };
    
    /**
     * @brief Construct thread pool with specified number of threads
     * @param numThreads Number of worker threads (0 = hardware concurrency)
     */
    explicit ThreadPool(size_t numThreads = 0);
    
    /**
     * @brief Destructor ensures graceful shutdown
     */
    ~ThreadPool();
    
    /**
     * @brief Submit a task for execution
     * @tparam F Function type
     * @tparam Args Argument types
     * @param priority Task priority
     * @param f Function to execute
     * @param args Function arguments
     * @return Future for the task result
     */
    template<typename F, typename... Args>
    auto submit(Priority priority, F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        
        using return_type = typename std::result_of<F(Args...)>::type;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> result = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            
            if (m_stopping) {
                throw std::runtime_error("Cannot submit task to stopping thread pool");
            }
            
            m_tasks.emplace(priority, [task]() { (*task)(); });
        }
        
        m_condition.notify_one();
        return result;
    }
    
    /**
     * @brief Submit a task with normal priority
     */
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        return submit(Priority::NORMAL, std::forward<F>(f), std::forward<Args>(args)...);
    }
    
    /**
     * @brief Submit a task without waiting for result
     */
    template<typename F, typename... Args>
    void submitDetached(Priority priority, F&& f, Args&&... args) {
        auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            
            if (m_stopping) {
                throw std::runtime_error("Cannot submit task to stopping thread pool");
            }
            
            m_tasks.emplace(priority, std::move(task));
        }
        
        m_condition.notify_one();
    }
    
    /**
     * @brief Wait for all tasks to complete
     */
    void waitForAll();
    
    /**
     * @brief Shutdown the thread pool
     * @param waitForTasks If true, wait for pending tasks to complete
     */
    void shutdown(bool waitForTasks = true);
    
    /**
     * @brief Get number of worker threads
     */
    size_t getNumThreads() const { return m_threads.size(); }
    
    /**
     * @brief Get number of pending tasks
     */
    size_t getNumPendingTasks() const;
    
    /**
     * @brief Get thread pool statistics
     */
    PoolStats getStats() const;
    
    /**
     * @brief Set exception handler for uncaught exceptions in tasks
     */
    using ExceptionHandler = std::function<void(std::exception_ptr)>;
    void setExceptionHandler(ExceptionHandler handler);
    
private:
    /**
     * @brief Task with priority
     */
    struct Task {
        Priority priority;
        std::function<void()> func;
        
        Task(Priority p, std::function<void()> f) 
            : priority(p), func(std::move(f)) {}
        
        // Operator for priority queue (higher priority first)
        bool operator<(const Task& other) const {
            return priority < other.priority;
        }
    };
    
    /**
     * @brief Worker thread function
     */
    void workerThread(size_t threadId);
    
    // Thread management
    std::vector<std::thread> m_threads;
    std::priority_queue<Task> m_tasks;
    
    // Synchronization
    mutable std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::condition_variable m_completionCondition;
    
    // State
    std::atomic<bool> m_stopping;
    std::atomic<size_t> m_numBusyThreads;
    
    // Statistics
    mutable std::mutex m_statsMutex;
    std::atomic<size_t> m_totalTasksExecuted;
    std::atomic<size_t> m_totalTasksFailed;
    std::chrono::nanoseconds m_totalExecutionTime;
    
    // Exception handling
    ExceptionHandler m_exceptionHandler;
};

/**
 * @brief Lock-free queue for high-performance scenarios
 */
template<typename T>
class LockFreeQueue {
public:
    LockFreeQueue() : m_head(new Node), m_tail(m_head.load()) {}
    
    ~LockFreeQueue() {
        while (Node* oldHead = m_head.load()) {
            m_head.store(oldHead->next);
            delete oldHead;
        }
    }
    
    void push(T item) {
        Node* newNode = new Node(std::move(item));
        Node* prevTail = m_tail.exchange(newNode);
        prevTail->next.store(newNode);
    }
    
    bool pop(T& item) {
        Node* head = m_head.load();
        
        do {
            Node* next = head->next.load();
            if (next == nullptr) {
                return false;
            }
            
            if (m_head.compare_exchange_weak(head, next)) {
                item = std::move(next->data);
                delete head;
                return true;
            }
        } while (true);
    }
    
    bool empty() const {
        return m_head.load()->next.load() == nullptr;
    }
    
private:
    struct Node {
        std::atomic<Node*> next;
        T data;
        
        Node() : next(nullptr) {}
        explicit Node(T&& d) : next(nullptr), data(std::move(d)) {}
    };
    
    std::atomic<Node*> m_head;
    std::atomic<Node*> m_tail;
};

/**
 * @brief Async task executor with cancellation support
 */
class AsyncTaskExecutor {
public:
    using TaskId = std::string;
    using TaskFunc = std::function<void(std::atomic<bool>& shouldStop)>;
    
    AsyncTaskExecutor(size_t numThreads = 0);
    ~AsyncTaskExecutor();
    
    /**
     * @brief Submit a cancellable task
     */
    TaskId submitTask(TaskFunc task, ThreadPool::Priority priority = ThreadPool::Priority::NORMAL);
    
    /**
     * @brief Cancel a task
     */
    bool cancelTask(const TaskId& taskId);
    
    /**
     * @brief Check if task is running
     */
    bool isTaskRunning(const TaskId& taskId) const;
    
    /**
     * @brief Wait for task completion
     */
    bool waitForTask(const TaskId& taskId, int timeoutMs = -1);
    
private:
    struct TaskInfo {
        std::atomic<bool> shouldStop;
        std::future<void> future;
        std::chrono::steady_clock::time_point startTime;
    };
    
    ThreadPool m_threadPool;
    mutable std::mutex m_tasksMutex;
    std::map<TaskId, std::unique_ptr<TaskInfo>> m_activeTasks;
    std::atomic<size_t> m_taskCounter;
    
    TaskId generateTaskId();
    void cleanupCompletedTasks();
};

} // namespace burwell

#endif // BURWELL_THREAD_POOL_H