#ifndef BURWELL_EVENT_MANAGER_H
#define BURWELL_EVENT_MANAGER_H

#include <functional>
#include <vector>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

namespace burwell {

// Event types for orchestrator communication
enum class OrchestratorEvent {
    USER_REQUEST,
    TASK_COMPLETED,
    TASK_FAILED,
    ENVIRONMENT_CHANGED,
    EMERGENCY_STOP,
    EXECUTION_STARTED,
    EXECUTION_PAUSED,
    EXECUTION_RESUMED,
    COMMAND_EXECUTED,
    ERROR_OCCURRED,
    USER_INTERACTION_REQUIRED,
    USER_INTERACTION_RECEIVED
};

// Event data structure
struct EventData {
    OrchestratorEvent type;
    std::string data;
    nlohmann::json metadata;
    std::string requestId;
    std::chrono::steady_clock::time_point timestamp;
    
    EventData(OrchestratorEvent t, const std::string& d) 
        : type(t), data(d), timestamp(std::chrono::steady_clock::now()) {}
    
    EventData(OrchestratorEvent t, const std::string& d, const nlohmann::json& meta)
        : type(t), data(d), metadata(meta), timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * @class EventManager
 * @brief Manages event handling and distribution in the orchestrator
 * 
 * This class provides a centralized event system for components to
 * communicate asynchronously. It supports multiple listeners per event type.
 */
class EventManager {
public:
    using EventListener = std::function<void(const EventData&)>;
    using EventFilter = std::function<bool(const EventData&)>;

    EventManager();
    ~EventManager();

    // Listener management
    void addEventListener(EventListener listener);
    void addEventListener(OrchestratorEvent eventType, EventListener listener);
    void addFilteredListener(EventFilter filter, EventListener listener);
    void removeAllListeners();
    size_t getListenerCount() const;

    // Event raising
    void raiseEvent(const EventData& event);
    void raiseEvent(OrchestratorEvent type, const std::string& data);
    void raiseEvent(OrchestratorEvent type, const std::string& data, const nlohmann::json& metadata);
    void raiseEvent(OrchestratorEvent type, const std::string& data, const std::string& requestId);

    // Event history
    void enableEventHistory(bool enable);
    void setMaxHistorySize(size_t maxSize);
    std::vector<EventData> getEventHistory() const;
    std::vector<EventData> getEventHistory(OrchestratorEvent type) const;
    void clearEventHistory();

    // Event statistics
    struct EventStatistics {
        std::map<OrchestratorEvent, size_t> eventCounts;
        std::chrono::steady_clock::time_point firstEvent;
        std::chrono::steady_clock::time_point lastEvent;
        size_t totalEvents;
    };
    
    EventStatistics getStatistics() const;
    void resetStatistics();

    // Utility methods
    static std::string eventTypeToString(OrchestratorEvent type);
    static OrchestratorEvent stringToEventType(const std::string& typeStr);

private:
    // Listener storage
    struct ListenerEntry {
        EventListener listener;
        EventFilter filter;
        OrchestratorEvent specificType;
        bool listenToAll;
        
        ListenerEntry(EventListener l) 
            : listener(l), specificType(OrchestratorEvent::USER_REQUEST), listenToAll(true) {}
        
        ListenerEntry(OrchestratorEvent type, EventListener l)
            : listener(l), specificType(type), listenToAll(false) {}
            
        ListenerEntry(EventFilter f, EventListener l)
            : listener(l), filter(f), specificType(OrchestratorEvent::USER_REQUEST), listenToAll(true) {}
    };
    
    std::vector<ListenerEntry> m_listeners;
    mutable std::mutex m_listenerMutex;
    
    // Event history
    std::vector<EventData> m_eventHistory;
    mutable std::mutex m_historyMutex;
    bool m_historyEnabled;
    size_t m_maxHistorySize;
    
    // Statistics
    EventStatistics m_statistics;
    mutable std::mutex m_statsMutex;
    
    // Helper methods
    void notifyListeners(const EventData& event);
    void updateStatistics(const EventData& event);
    void enforceHistoryLimit();
};

} // namespace burwell

#endif // BURWELL_EVENT_MANAGER_H