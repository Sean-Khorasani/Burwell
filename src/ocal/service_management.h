#ifndef BURWELL_SERVICE_MANAGEMENT_H
#define BURWELL_SERVICE_MANAGEMENT_H

#include <string>
#include <vector>

namespace ocal {
namespace service {

enum class ServiceState {
    STOPPED,
    START_PENDING,
    STOP_PENDING,
    RUNNING,
    CONTINUE_PENDING,
    PAUSE_PENDING,
    PAUSED,
    UNKNOWN
};

struct ServiceInfo {
    std::string name;
    std::string displayName;
    std::string description;
    ServiceState state;
    bool isAutoStart;
    
    ServiceInfo() : state(ServiceState::UNKNOWN), isAutoStart(false) {}
};

// Core service management functions
bool start(const std::string& serviceName);
bool stop(const std::string& serviceName);
bool restart(const std::string& serviceName);
bool pause(const std::string& serviceName);
bool resume(const std::string& serviceName);

// Service information
ServiceInfo getInfo(const std::string& serviceName);
ServiceState getState(const std::string& serviceName);
std::vector<ServiceInfo> getAll();
bool exists(const std::string& serviceName);

// Service control
bool setStartType(const std::string& serviceName, bool autoStart);
bool waitForState(const std::string& serviceName, ServiceState targetState, int timeoutMs = 30000);

// Utility functions
std::string stateToString(ServiceState state);
ServiceState stringToState(const std::string& stateStr);

} // namespace service
} // namespace ocal

#endif // BURWELL_SERVICE_MANAGEMENT_H