#ifndef BURWELL_CONNECTION_POOL_H
#define BURWELL_CONNECTION_POOL_H

#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <atomic>
#include <map>

namespace burwell {

/**
 * @brief Generic connection pool for managing reusable connections
 * @tparam Connection The connection type to pool
 */
template<typename Connection>
class ConnectionPool {
public:
    using ConnectionPtr = std::shared_ptr<Connection>;
    using ConnectionFactory = std::function<ConnectionPtr()>;
    using ConnectionValidator = std::function<bool(const ConnectionPtr&)>;
    using ConnectionReset = std::function<void(ConnectionPtr&)>;
    
    /**
     * @brief Construct a new connection pool
     * @param factory Function to create new connections
     * @param maxSize Maximum number of connections to maintain
     */
    ConnectionPool(ConnectionFactory factory, size_t maxSize = 10)
        : m_factory(factory)
        , m_maxSize(maxSize)
        , m_activeConnections(0)
        , m_totalConnectionsCreated(0)
        , m_isShuttingDown(false) {
        
        if (!m_factory) {
            throw std::invalid_argument("Connection factory cannot be null");
        }
    }
    
    ~ConnectionPool() {
        shutdown();
    }
    
    /**
     * @brief RAII wrapper for automatic connection return
     */
    class PooledConnection {
    public:
        PooledConnection(ConnectionPtr conn, ConnectionPool* pool)
            : m_connection(conn), m_pool(pool), m_released(false) {}
            
        ~PooledConnection() {
            release();
        }
        
        // Move constructor
        PooledConnection(PooledConnection&& other) noexcept
            : m_connection(std::move(other.m_connection))
            , m_pool(other.m_pool)
            , m_released(other.m_released) {
            other.m_released = true;
        }
        
        // Move assignment
        PooledConnection& operator=(PooledConnection&& other) noexcept {
            if (this != &other) {
                release();
                m_connection = std::move(other.m_connection);
                m_pool = other.m_pool;
                m_released = other.m_released;
                other.m_released = true;
            }
            return *this;
        }
        
        // Delete copy operations
        PooledConnection(const PooledConnection&) = delete;
        PooledConnection& operator=(const PooledConnection&) = delete;
        
        Connection* operator->() const {
            if (!m_connection) {
                throw std::runtime_error("Accessing null pooled connection");
            }
            return m_connection.get();
        }
        
        Connection& operator*() const {
            if (!m_connection) {
                throw std::runtime_error("Dereferencing null pooled connection");
            }
            return *m_connection;
        }
        
        ConnectionPtr get() const {
            return m_connection;
        }
        
        bool isValid() const {
            return m_connection != nullptr;
        }
        
        void release() {
            if (!m_released && m_connection && m_pool) {
                m_pool->returnConnection(m_connection);
                m_released = true;
            }
        }
        
    private:
        ConnectionPtr m_connection;
        ConnectionPool* m_pool;
        bool m_released;
    };
    
    /**
     * @brief Acquire a connection from the pool
     * @param timeoutMs Maximum time to wait for a connection
     * @return Pooled connection wrapper
     */
    PooledConnection acquire(int timeoutMs = 5000) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        if (m_isShuttingDown) {
            throw std::runtime_error("Connection pool is shutting down");
        }
        
        auto deadline = std::chrono::steady_clock::now() + 
                       std::chrono::milliseconds(timeoutMs);
        
        // Try to get a connection from the pool
        while (m_availableConnections.empty() && 
               m_activeConnections >= m_maxSize) {
            if (m_condition.wait_until(lock, deadline) == std::cv_status::timeout) {
                throw std::runtime_error("Timeout waiting for connection");
            }
            
            if (m_isShuttingDown) {
                throw std::runtime_error("Connection pool is shutting down");
            }
        }
        
        ConnectionPtr conn;
        
        // Try to reuse an existing connection
        while (!m_availableConnections.empty()) {
            conn = m_availableConnections.front();
            m_availableConnections.pop();
            
            if (validateConnection(conn)) {
                break;
            }
            
            // Connection is invalid, discard it
            conn.reset();
        }
        
        // Create a new connection if needed
        if (!conn && m_activeConnections < m_maxSize) {
            conn = createConnection();
        }
        
        if (!conn) {
            throw std::runtime_error("Failed to acquire connection");
        }
        
        m_activeConnections++;
        m_lastAcquireTime = std::chrono::steady_clock::now();
        
        return PooledConnection(conn, this);
    }
    
    /**
     * @brief Set connection validator function
     */
    void setValidator(ConnectionValidator validator) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_validator = validator;
    }
    
    /**
     * @brief Set connection reset function
     */
    void setReset(ConnectionReset reset) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_reset = reset;
    }
    
    /**
     * @brief Get pool statistics
     */
    struct PoolStats {
        size_t activeConnections;
        size_t availableConnections;
        size_t totalConnectionsCreated;
        size_t maxSize;
        std::chrono::steady_clock::time_point lastAcquireTime;
    };
    
    PoolStats getStats() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {
            m_activeConnections,
            m_availableConnections.size(),
            m_totalConnectionsCreated,
            m_maxSize,
            m_lastAcquireTime
        };
    }
    
    /**
     * @brief Clear all idle connections
     */
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_availableConnections.empty()) {
            m_availableConnections.pop();
        }
    }
    
    /**
     * @brief Shutdown the pool
     */
    void shutdown() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_isShuttingDown = true;
        
        // Clear available connections
        while (!m_availableConnections.empty()) {
            m_availableConnections.pop();
        }
        
        // Notify all waiting threads
        m_condition.notify_all();
        
        // Wait for active connections to be returned
        m_shutdownCondition.wait(lock, [this] {
            return m_activeConnections == 0;
        });
    }
    
private:
    ConnectionFactory m_factory;
    ConnectionValidator m_validator;
    ConnectionReset m_reset;
    
    std::queue<ConnectionPtr> m_availableConnections;
    size_t m_maxSize;
    std::atomic<size_t> m_activeConnections;
    std::atomic<size_t> m_totalConnectionsCreated;
    
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::condition_variable m_shutdownCondition;
    
    bool m_isShuttingDown;
    std::chrono::steady_clock::time_point m_lastAcquireTime;
    
    ConnectionPtr createConnection() {
        ConnectionPtr conn = m_factory();
        if (conn) {
            m_totalConnectionsCreated++;
        }
        return conn;
    }
    
    bool validateConnection(const ConnectionPtr& conn) {
        if (!conn) {
            return false;
        }
        
        if (m_validator) {
            return m_validator(conn);
        }
        
        return true;
    }
    
    void returnConnection(ConnectionPtr conn) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        m_activeConnections--;
        
        if (m_isShuttingDown) {
            m_shutdownCondition.notify_all();
            return;
        }
        
        if (conn && validateConnection(conn)) {
            // Reset connection if reset function provided
            if (m_reset) {
                m_reset(conn);
            }
            
            m_availableConnections.push(conn);
        }
        
        m_condition.notify_one();
    }
    
    friend class PooledConnection;
};

} // namespace burwell

#endif // BURWELL_CONNECTION_POOL_H