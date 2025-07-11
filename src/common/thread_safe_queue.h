#ifndef BURWELL_THREAD_SAFE_QUEUE_H
#define BURWELL_THREAD_SAFE_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>
#include <atomic>

namespace burwell {

/**
 * @brief Thread-safe queue implementation
 * @tparam T Element type
 */
template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() : m_closed(false) {}
    
    /**
     * @brief Push an item to the queue
     * @param item Item to push
     * @return true if successful, false if queue is closed
     */
    bool push(T item) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed) {
                return false;
            }
            m_queue.push(std::move(item));
        }
        m_condition.notify_one();
        return true;
    }
    
    /**
     * @brief Pop an item from the queue (blocking)
     * @return Optional containing the item, or empty if queue is closed
     */
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this] { return !m_queue.empty() || m_closed; });
        
        if (m_queue.empty()) {
            return std::nullopt;
        }
        
        T item = std::move(m_queue.front());
        m_queue.pop();
        return item;
    }
    
    /**
     * @brief Try to pop an item from the queue (non-blocking)
     * @return Optional containing the item, or empty if queue is empty
     */
    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return std::nullopt;
        }
        
        T item = std::move(m_queue.front());
        m_queue.pop();
        return item;
    }
    
    /**
     * @brief Pop an item with timeout
     * @param timeoutMs Timeout in milliseconds
     * @return Optional containing the item, or empty if timeout/closed
     */
    std::optional<T> popWithTimeout(int timeoutMs) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        if (!m_condition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                 [this] { return !m_queue.empty() || m_closed; })) {
            return std::nullopt;
        }
        
        if (m_queue.empty()) {
            return std::nullopt;
        }
        
        T item = std::move(m_queue.front());
        m_queue.pop();
        return item;
    }
    
    /**
     * @brief Get the size of the queue
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }
    
    /**
     * @brief Check if queue is empty
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }
    
    /**
     * @brief Close the queue (no more items can be pushed)
     */
    void close() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_condition.notify_all();
    }
    
    /**
     * @brief Check if queue is closed
     */
    bool isClosed() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed;
    }
    
    /**
     * @brief Clear all items from the queue
     */
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_queue.empty()) {
            m_queue.pop();
        }
    }
    
private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::queue<T> m_queue;
    bool m_closed;
};

/**
 * @brief Priority queue with thread safety
 * @tparam T Element type
 * @tparam Compare Comparison function
 */
template<typename T, typename Compare = std::less<T>>
class ThreadSafePriorityQueue {
public:
    ThreadSafePriorityQueue() : m_closed(false) {}
    
    /**
     * @brief Push an item to the priority queue
     */
    bool push(T item) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed) {
                return false;
            }
            m_queue.push(std::move(item));
        }
        m_condition.notify_one();
        return true;
    }
    
    /**
     * @brief Pop the highest priority item (blocking)
     */
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this] { return !m_queue.empty() || m_closed; });
        
        if (m_queue.empty()) {
            return std::nullopt;
        }
        
        T item = m_queue.top();
        m_queue.pop();
        return item;
    }
    
    /**
     * @brief Try to pop the highest priority item (non-blocking)
     */
    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return std::nullopt;
        }
        
        T item = m_queue.top();
        m_queue.pop();
        return item;
    }
    
    /**
     * @brief Get the size of the queue
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }
    
    /**
     * @brief Check if queue is empty
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }
    
    /**
     * @brief Close the queue
     */
    void close() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_condition.notify_all();
    }
    
    /**
     * @brief Check if queue is closed
     */
    bool isClosed() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed;
    }
    
    /**
     * @brief Clear all items
     */
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_queue.empty()) {
            m_queue.pop();
        }
    }
    
private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::priority_queue<T, std::vector<T>, Compare> m_queue;
    bool m_closed;
};

/**
 * @brief Simple spinlock for low-contention scenarios
 */
class SpinLock {
public:
    SpinLock() : m_flag(ATOMIC_FLAG_INIT) {}
    
    void lock() {
        while (m_flag.test_and_set(std::memory_order_acquire)) {
            // Spin
        }
    }
    
    void unlock() {
        m_flag.clear(std::memory_order_release);
    }
    
    bool try_lock() {
        return !m_flag.test_and_set(std::memory_order_acquire);
    }
    
private:
    std::atomic_flag m_flag;
};

/**
 * @brief Reader-writer lock wrapper
 */
class ReaderWriterLock {
public:
    ReaderWriterLock() : m_readers(0), m_writers(0), m_writeRequests(0) {}
    
    void lockRead() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_readCondition.wait(lock, [this] { 
            return m_writers == 0 && m_writeRequests == 0; 
        });
        m_readers++;
    }
    
    void unlockRead() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_readers--;
        if (m_readers == 0) {
            m_writeCondition.notify_one();
        }
    }
    
    void lockWrite() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_writeRequests++;
        m_writeCondition.wait(lock, [this] { 
            return m_readers == 0 && m_writers == 0; 
        });
        m_writeRequests--;
        m_writers++;
    }
    
    void unlockWrite() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_writers--;
        m_writeCondition.notify_one();
        m_readCondition.notify_all();
    }
    
private:
    mutable std::mutex m_mutex;
    std::condition_variable m_readCondition;
    std::condition_variable m_writeCondition;
    int m_readers;
    int m_writers;
    int m_writeRequests;
};

/**
 * @brief RAII reader lock guard
 */
class ReadLock {
public:
    explicit ReadLock(ReaderWriterLock& lock) : m_lock(lock) {
        m_lock.lockRead();
    }
    
    ~ReadLock() {
        m_lock.unlockRead();
    }
    
    // Delete copy/move
    ReadLock(const ReadLock&) = delete;
    ReadLock& operator=(const ReadLock&) = delete;
    
private:
    ReaderWriterLock& m_lock;
};

/**
 * @brief RAII writer lock guard
 */
class WriteLock {
public:
    explicit WriteLock(ReaderWriterLock& lock) : m_lock(lock) {
        m_lock.lockWrite();
    }
    
    ~WriteLock() {
        m_lock.unlockWrite();
    }
    
    // Delete copy/move
    WriteLock(const WriteLock&) = delete;
    WriteLock& operator=(const WriteLock&) = delete;
    
private:
    ReaderWriterLock& m_lock;
};

} // namespace burwell

#endif // BURWELL_THREAD_SAFE_QUEUE_H