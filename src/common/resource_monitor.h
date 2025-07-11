#ifndef BURWELL_RESOURCE_MONITOR_H
#define BURWELL_RESOURCE_MONITOR_H

#include <atomic>
#include <mutex>
#include <map>
#include <string>
#include <memory>
#include <chrono>
#include <vector>
#include <functional>

namespace burwell {

/**
 * @brief Resource tracking and leak detection
 * 
 * Tracks resource allocation/deallocation to detect leaks in debug builds
 */
class ResourceMonitor {
public:
    enum class ResourceType {
        MEMORY,
        FILE_HANDLE,
        WINDOW_HANDLE,
        PROCESS_HANDLE,
        REGISTRY_HANDLE,
        SOCKET,
        THREAD,
        MUTEX,
        OTHER
    };
    
    struct ResourceInfo {
        ResourceType type;
        std::string description;
        void* address;
        size_t size;
        std::chrono::steady_clock::time_point allocTime;
        std::string stackTrace;
        bool isLeaked;
        
        ResourceInfo() : address(nullptr), size(0), isLeaked(false) {}
    };
    
    static ResourceMonitor& getInstance() {
        static ResourceMonitor instance;
        return instance;
    }
    
    // Track resource allocation
    void trackAllocation(ResourceType type, void* address, size_t size, 
                        const std::string& description = "");
    
    // Track resource deallocation
    void trackDeallocation(void* address);
    
    // Check for leaks
    std::vector<ResourceInfo> detectLeaks() const;
    
    // Get resource statistics
    struct ResourceStats {
        std::map<ResourceType, size_t> activeCount;
        std::map<ResourceType, size_t> totalAllocated;
        std::map<ResourceType, size_t> totalDeallocated;
        std::map<ResourceType, size_t> peakUsage;
        size_t totalMemoryUsage;
        size_t peakMemoryUsage;
    };
    
    ResourceStats getStats() const;
    
    // Clear all tracking data
    void reset();
    
    // Enable/disable tracking
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    
    // Set leak reporting callback
    using LeakCallback = std::function<void(const std::vector<ResourceInfo>&)>;
    void setLeakCallback(LeakCallback callback) { m_leakCallback = callback; }
    
    // Report leaks (called on shutdown)
    void reportLeaks();
    
private:
    ResourceMonitor();
    ~ResourceMonitor();
    
    // Delete copy/move operations
    ResourceMonitor(const ResourceMonitor&) = delete;
    ResourceMonitor& operator=(const ResourceMonitor&) = delete;
    
    std::atomic<bool> m_enabled;
    mutable std::mutex m_mutex;
    
    struct ResourceEntry {
        ResourceInfo info;
        bool deallocated;
    };
    
    std::map<void*, ResourceEntry> m_resources;
    std::map<ResourceType, size_t> m_activeCount;
    std::map<ResourceType, size_t> m_totalAllocated;
    std::map<ResourceType, size_t> m_totalDeallocated;
    std::map<ResourceType, size_t> m_peakUsage;
    
    std::atomic<size_t> m_totalMemoryUsage;
    std::atomic<size_t> m_peakMemoryUsage;
    
    LeakCallback m_leakCallback;
    
    // Helper to get stack trace (platform-specific)
    std::string captureStackTrace();
    
    // Update peak usage statistics
    void updatePeakUsage(ResourceType type);
};

/**
 * @brief RAII helper for automatic resource tracking
 */
template<typename T>
class TrackedResource {
public:
    TrackedResource(ResourceMonitor::ResourceType type, T* resource, 
                   size_t size = sizeof(T), const std::string& desc = "")
        : m_resource(resource), m_type(type) {
        if (m_resource) {
            ResourceMonitor::getInstance().trackAllocation(
                m_type, m_resource, size, desc);
        }
    }
    
    ~TrackedResource() {
        if (m_resource) {
            ResourceMonitor::getInstance().trackDeallocation(m_resource);
        }
    }
    
    // Move constructor
    TrackedResource(TrackedResource&& other) noexcept
        : m_resource(other.m_resource), m_type(other.m_type) {
        other.m_resource = nullptr;
    }
    
    // Move assignment
    TrackedResource& operator=(TrackedResource&& other) noexcept {
        if (this != &other) {
            if (m_resource) {
                ResourceMonitor::getInstance().trackDeallocation(m_resource);
            }
            m_resource = other.m_resource;
            m_type = other.m_type;
            other.m_resource = nullptr;
        }
        return *this;
    }
    
    // Delete copy operations
    TrackedResource(const TrackedResource&) = delete;
    TrackedResource& operator=(const TrackedResource&) = delete;
    
    T* get() const { return m_resource; }
    T* release() {
        T* temp = m_resource;
        m_resource = nullptr;
        return temp;
    }
    
    void reset(T* resource = nullptr) {
        if (m_resource) {
            ResourceMonitor::getInstance().trackDeallocation(m_resource);
        }
        m_resource = resource;
        if (m_resource) {
            ResourceMonitor::getInstance().trackAllocation(
                m_type, m_resource, sizeof(T));
        }
    }
    
private:
    T* m_resource;
    ResourceMonitor::ResourceType m_type;
};

// Convenience macros for debug builds
#ifdef DEBUG
    #define TRACK_RESOURCE(type, ptr, size, desc) \
        ResourceMonitor::getInstance().trackAllocation(type, ptr, size, desc)
    
    #define UNTRACK_RESOURCE(ptr) \
        ResourceMonitor::getInstance().trackDeallocation(ptr)
    
    #define REPORT_LEAKS() \
        ResourceMonitor::getInstance().reportLeaks()
#else
    #define TRACK_RESOURCE(type, ptr, size, desc) ((void)0)
    #define UNTRACK_RESOURCE(ptr) ((void)0)
    #define REPORT_LEAKS() ((void)0)
#endif

} // namespace burwell

#endif // BURWELL_RESOURCE_MONITOR_H