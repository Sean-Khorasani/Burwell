#ifndef BURWELL_SHUTDOWN_MANAGER_H
#define BURWELL_SHUTDOWN_MANAGER_H

#include <atomic>

namespace burwell {

/**
 * @brief Global shutdown manager for handling graceful shutdown
 */
class ShutdownManager {
public:
    static ShutdownManager& getInstance() {
        static ShutdownManager instance;
        return instance;
    }
    
    void requestShutdown() {
        m_shutdown_requested = true;
    }
    
    bool isShutdownRequested() const {
        return m_shutdown_requested.load();
    }
    
    void incrementCtrlCCount() {
        m_ctrl_c_count++;
    }
    
    int getCtrlCCount() const {
        return m_ctrl_c_count.load();
    }
    
    void reset() {
        m_shutdown_requested = false;
        m_ctrl_c_count = 0;
    }
    
private:
    ShutdownManager() : m_shutdown_requested(false), m_ctrl_c_count(0) {}
    
    std::atomic<bool> m_shutdown_requested;
    std::atomic<int> m_ctrl_c_count;
};

} // namespace burwell

#endif // BURWELL_SHUTDOWN_MANAGER_H