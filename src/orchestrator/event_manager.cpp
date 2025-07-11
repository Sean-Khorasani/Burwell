#include "event_manager.h"
#include "../common/structured_logger.h"
#include <algorithm>
#include <sstream>

namespace burwell {

EventManager::EventManager()
    : m_historyEnabled(false)
    , m_maxHistorySize(1000) {
    m_statistics.totalEvents = 0;
    m_statistics.firstEvent = std::chrono::steady_clock::now();
    m_statistics.lastEvent = std::chrono::steady_clock::now();
    SLOG_DEBUG().message("EventManager initialized");
}

EventManager::~EventManager() {
    SLOG_DEBUG().message("EventManager destroyed");
}

void EventManager::addEventListener(EventListener listener) {
    std::lock_guard<std::mutex> lock(m_listenerMutex);
    m_listeners.emplace_back(listener);
}

void EventManager::addEventListener(OrchestratorEvent eventType, EventListener listener) {
    std::lock_guard<std::mutex> lock(m_listenerMutex);
    m_listeners.emplace_back(eventType, listener);
}

void EventManager::addFilteredListener(EventFilter filter, EventListener listener) {
    std::lock_guard<std::mutex> lock(m_listenerMutex);
    m_listeners.emplace_back(filter, listener);
}

void EventManager::removeAllListeners() {
    std::lock_guard<std::mutex> lock(m_listenerMutex);
    m_listeners.clear();
    SLOG_DEBUG().message("All event listeners removed");
}

size_t EventManager::getListenerCount() const {
    std::lock_guard<std::mutex> lock(m_listenerMutex);
    return m_listeners.size();
}

void EventManager::raiseEvent(const EventData& event) {
    // Log the event
    SLOG_DEBUG().message("Event raised").context("type", eventTypeToString(event.type)).context("data", event.data);
    
    // Update statistics
    updateStatistics(event);
    
    // Add to history if enabled
    if (m_historyEnabled) {
        std::lock_guard<std::mutex> lock(m_historyMutex);
        m_eventHistory.push_back(event);
        enforceHistoryLimit();
    }
    
    // Notify listeners
    notifyListeners(event);
}

void EventManager::raiseEvent(OrchestratorEvent type, const std::string& data) {
    EventData event(type, data);
    raiseEvent(event);
}

void EventManager::raiseEvent(OrchestratorEvent type, const std::string& data, const nlohmann::json& metadata) {
    EventData event(type, data, metadata);
    raiseEvent(event);
}

void EventManager::raiseEvent(OrchestratorEvent type, const std::string& data, const std::string& requestId) {
    EventData event(type, data);
    event.requestId = requestId;
    raiseEvent(event);
}

void EventManager::enableEventHistory(bool enable) {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    m_historyEnabled = enable;
    if (!enable) {
        m_eventHistory.clear();
    }
}

void EventManager::setMaxHistorySize(size_t maxSize) {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    m_maxHistorySize = maxSize;
    enforceHistoryLimit();
}

std::vector<EventData> EventManager::getEventHistory() const {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    return m_eventHistory;
}

std::vector<EventData> EventManager::getEventHistory(OrchestratorEvent type) const {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    std::vector<EventData> filteredHistory;
    
    for (const auto& event : m_eventHistory) {
        if (event.type == type) {
            filteredHistory.push_back(event);
        }
    }
    
    return filteredHistory;
}

void EventManager::clearEventHistory() {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    m_eventHistory.clear();
}

EventManager::EventStatistics EventManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_statistics;
}

void EventManager::resetStatistics() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_statistics.eventCounts.clear();
    m_statistics.totalEvents = 0;
    m_statistics.firstEvent = std::chrono::steady_clock::now();
    m_statistics.lastEvent = std::chrono::steady_clock::now();
}

std::string EventManager::eventTypeToString(OrchestratorEvent type) {
    switch (type) {
        case OrchestratorEvent::USER_REQUEST: return "USER_REQUEST";
        case OrchestratorEvent::TASK_COMPLETED: return "TASK_COMPLETED";
        case OrchestratorEvent::TASK_FAILED: return "TASK_FAILED";
        case OrchestratorEvent::ENVIRONMENT_CHANGED: return "ENVIRONMENT_CHANGED";
        case OrchestratorEvent::EMERGENCY_STOP: return "EMERGENCY_STOP";
        case OrchestratorEvent::EXECUTION_STARTED: return "EXECUTION_STARTED";
        case OrchestratorEvent::EXECUTION_PAUSED: return "EXECUTION_PAUSED";
        case OrchestratorEvent::EXECUTION_RESUMED: return "EXECUTION_RESUMED";
        case OrchestratorEvent::COMMAND_EXECUTED: return "COMMAND_EXECUTED";
        case OrchestratorEvent::ERROR_OCCURRED: return "ERROR_OCCURRED";
        case OrchestratorEvent::USER_INTERACTION_REQUIRED: return "USER_INTERACTION_REQUIRED";
        case OrchestratorEvent::USER_INTERACTION_RECEIVED: return "USER_INTERACTION_RECEIVED";
        default: return "UNKNOWN";
    }
}

OrchestratorEvent EventManager::stringToEventType(const std::string& typeStr) {
    if (typeStr == "USER_REQUEST") return OrchestratorEvent::USER_REQUEST;
    if (typeStr == "TASK_COMPLETED") return OrchestratorEvent::TASK_COMPLETED;
    if (typeStr == "TASK_FAILED") return OrchestratorEvent::TASK_FAILED;
    if (typeStr == "ENVIRONMENT_CHANGED") return OrchestratorEvent::ENVIRONMENT_CHANGED;
    if (typeStr == "EMERGENCY_STOP") return OrchestratorEvent::EMERGENCY_STOP;
    if (typeStr == "EXECUTION_STARTED") return OrchestratorEvent::EXECUTION_STARTED;
    if (typeStr == "EXECUTION_PAUSED") return OrchestratorEvent::EXECUTION_PAUSED;
    if (typeStr == "EXECUTION_RESUMED") return OrchestratorEvent::EXECUTION_RESUMED;
    if (typeStr == "COMMAND_EXECUTED") return OrchestratorEvent::COMMAND_EXECUTED;
    if (typeStr == "ERROR_OCCURRED") return OrchestratorEvent::ERROR_OCCURRED;
    if (typeStr == "USER_INTERACTION_REQUIRED") return OrchestratorEvent::USER_INTERACTION_REQUIRED;
    if (typeStr == "USER_INTERACTION_RECEIVED") return OrchestratorEvent::USER_INTERACTION_RECEIVED;
    
    // Default
    return OrchestratorEvent::USER_REQUEST;
}

void EventManager::notifyListeners(const EventData& event) {
    // Create a copy of listeners to avoid holding lock during callbacks
    std::vector<ListenerEntry> listenersCopy;
    {
        std::lock_guard<std::mutex> lock(m_listenerMutex);
        listenersCopy = m_listeners;
    }
    
    // Notify each listener
    for (const auto& entry : listenersCopy) {
        try {
            bool shouldNotify = false;
            
            if (entry.listenToAll) {
                // Listener registered for all events
                if (entry.filter) {
                    // Has a filter, check it
                    shouldNotify = entry.filter(event);
                } else {
                    // No filter, always notify
                    shouldNotify = true;
                }
            } else {
                // Listener registered for specific event type
                shouldNotify = (entry.specificType == event.type);
            }
            
            if (shouldNotify) {
                entry.listener(event);
            }
        } catch (const std::exception& e) {
            SLOG_ERROR().message("Exception in event listener").context("error", e.what());
        } catch (...) {
            SLOG_ERROR().message("Unknown exception in event listener");
        }
    }
}

void EventManager::updateStatistics(const EventData& event) {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    
    // Update event count
    m_statistics.eventCounts[event.type]++;
    m_statistics.totalEvents++;
    
    // Update timestamps
    if (m_statistics.totalEvents == 1) {
        m_statistics.firstEvent = event.timestamp;
    }
    m_statistics.lastEvent = event.timestamp;
}

void EventManager::enforceHistoryLimit() {
    // Should be called with m_historyMutex already locked
    while (m_eventHistory.size() > m_maxHistorySize) {
        m_eventHistory.erase(m_eventHistory.begin());
    }
}

} // namespace burwell