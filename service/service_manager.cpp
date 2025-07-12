#include "service_manager.h"
#include <iostream>
#include <sstream>
#include <filesystem>

namespace burwell {

ServiceManager::ServiceManager() = default;
ServiceManager::~ServiceManager() = default;

bool ServiceManager::installService(const std::string& burwellPath,
                                   const std::string& serviceName,
                                   const std::string& displayName,
                                   const std::string& description) {
    // Validate burwell.exe exists
    if (!std::filesystem::exists(burwellPath)) {
        m_lastError = "Burwell executable not found at: " + burwellPath;
        return false;
    }

    SC_HANDLE scManager = openSCManager(SC_MANAGER_CREATE_SERVICE);
    if (!scManager) {
        setLastError("Failed to open Service Control Manager");
        return false;
    }

    // Create service command line with daemon flag
    std::string serviceCommand = "\"" + burwellPath + "\" --daemon";

    SC_HANDLE service = CreateServiceA(
        scManager,                    // SCM database handle
        serviceName.c_str(),          // Service name
        displayName.c_str(),          // Display name
        SERVICE_ALL_ACCESS,           // Desired access
        SERVICE_WIN32_OWN_PROCESS,    // Service type
        SERVICE_AUTO_START,           // Start type (automatic)
        SERVICE_ERROR_NORMAL,         // Error control type
        serviceCommand.c_str(),       // Path to service binary
        nullptr,                      // No load ordering group
        nullptr,                      // No tag identifier
        nullptr,                      // No service dependencies
        nullptr,                      // LocalSystem account
        nullptr                       // No password
    );

    if (!service) {
        setLastError("Failed to create service");
        CloseServiceHandle(scManager);
        return false;
    }

    // Set service description
    SERVICE_DESCRIPTIONA serviceDesc;
    serviceDesc.lpDescription = const_cast<char*>(description.c_str());
    ChangeServiceConfig2A(service, SERVICE_CONFIG_DESCRIPTION, &serviceDesc);

    // Configure service to restart on failure
    SC_ACTION failureActions[3];
    failureActions[0].Type = SC_ACTION_RESTART;
    failureActions[0].Delay = 5000; // 5 seconds
    failureActions[1].Type = SC_ACTION_RESTART;
    failureActions[1].Delay = 10000; // 10 seconds
    failureActions[2].Type = SC_ACTION_NONE;
    failureActions[2].Delay = 0;

    SERVICE_FAILURE_ACTIONSA failureConfig;
    failureConfig.dwResetPeriod = 3600; // Reset failure count after 1 hour
    failureConfig.lpRebootMsg = nullptr;
    failureConfig.lpCommand = nullptr;
    failureConfig.cActions = 3;
    failureConfig.lpsaActions = failureActions;

    ChangeServiceConfig2A(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failureConfig);

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    return true;
}

bool ServiceManager::uninstallService(const std::string& serviceName) {
    SC_HANDLE scManager = openSCManager();
    if (!scManager) {
        setLastError("Failed to open Service Control Manager");
        return false;
    }

    SC_HANDLE service = openService(serviceName, DELETE);
    if (!service) {
        setLastError("Failed to open service: " + serviceName);
        CloseServiceHandle(scManager);
        return false;
    }

    // Stop service if running
    stopService(serviceName);

    // Delete the service
    if (!DeleteService(service)) {
        setLastError("Failed to delete service");
        CloseServiceHandle(service);
        CloseServiceHandle(scManager);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    return true;
}

bool ServiceManager::startService(const std::string& serviceName) {
    SC_HANDLE scManager = openSCManager();
    if (!scManager) {
        setLastError("Failed to open Service Control Manager");
        return false;
    }

    SC_HANDLE service = openService(serviceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service) {
        setLastError("Failed to open service: " + serviceName);
        CloseServiceHandle(scManager);
        return false;
    }

    // Check if already running
    SERVICE_STATUS status;
    if (QueryServiceStatus(service, &status)) {
        if (status.dwCurrentState == SERVICE_RUNNING) {
            CloseServiceHandle(service);
            CloseServiceHandle(scManager);
            return true; // Already running
        }
    }

    // Start the service
    if (!StartServiceA(service, 0, nullptr)) {
        setLastError("Failed to start service");
        CloseServiceHandle(service);
        CloseServiceHandle(scManager);
        return false;
    }

    // Wait for service to start
    bool success = waitForServiceState(service, SERVICE_RUNNING);

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    return success;
}

bool ServiceManager::stopService(const std::string& serviceName) {
    SC_HANDLE scManager = openSCManager();
    if (!scManager) {
        setLastError("Failed to open Service Control Manager");
        return false;
    }

    SC_HANDLE service = openService(serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!service) {
        setLastError("Failed to open service: " + serviceName);
        CloseServiceHandle(scManager);
        return false;
    }

    // Check if already stopped
    SERVICE_STATUS status;
    if (QueryServiceStatus(service, &status)) {
        if (status.dwCurrentState == SERVICE_STOPPED) {
            CloseServiceHandle(service);
            CloseServiceHandle(scManager);
            return true; // Already stopped
        }
    }

    // Stop the service
    if (!ControlService(service, SERVICE_CONTROL_STOP, &status)) {
        setLastError("Failed to stop service");
        CloseServiceHandle(service);
        CloseServiceHandle(scManager);
        return false;
    }

    // Wait for service to stop
    bool success = waitForServiceState(service, SERVICE_STOPPED);

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    return success;
}

std::string ServiceManager::getServiceStatus(const std::string& serviceName) {
    if (!isServiceInstalled(serviceName)) {
        return "NOT_INSTALLED";
    }

    SC_HANDLE scManager = openSCManager(SC_MANAGER_CONNECT);
    if (!scManager) {
        return "ERROR: Cannot connect to Service Control Manager";
    }

    SC_HANDLE service = openService(serviceName, SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(scManager);
        return "ERROR: Cannot open service";
    }

    SERVICE_STATUS status;
    if (!QueryServiceStatus(service, &status)) {
        CloseServiceHandle(service);
        CloseServiceHandle(scManager);
        return "ERROR: Cannot query service status";
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    switch (status.dwCurrentState) {
        case SERVICE_STOPPED: return "STOPPED";
        case SERVICE_START_PENDING: return "START_PENDING";
        case SERVICE_STOP_PENDING: return "STOP_PENDING";
        case SERVICE_RUNNING: return "RUNNING";
        case SERVICE_CONTINUE_PENDING: return "CONTINUE_PENDING";
        case SERVICE_PAUSE_PENDING: return "PAUSE_PENDING";
        case SERVICE_PAUSED: return "PAUSED";
        default: return "UNKNOWN";
    }
}

bool ServiceManager::isServiceInstalled(const std::string& serviceName) {
    SC_HANDLE scManager = openSCManager(SC_MANAGER_CONNECT);
    if (!scManager) {
        return false;
    }

    SC_HANDLE service = OpenServiceA(scManager, serviceName.c_str(), SERVICE_QUERY_CONFIG);
    bool installed = (service != nullptr);

    if (service) {
        CloseServiceHandle(service);
    }
    CloseServiceHandle(scManager);

    return installed;
}

SC_HANDLE ServiceManager::openSCManager(DWORD access) {
    SC_HANDLE scManager = OpenSCManagerA(nullptr, nullptr, access);
    if (!scManager) {
        setLastError("Failed to open Service Control Manager");
    }
    return scManager;
}

SC_HANDLE ServiceManager::openService(const std::string& serviceName, DWORD access) {
    SC_HANDLE scManager = openSCManager();
    if (!scManager) {
        return nullptr;
    }

    SC_HANDLE service = OpenServiceA(scManager, serviceName.c_str(), access);
    if (!service) {
        setLastError("Failed to open service: " + serviceName);
    }

    CloseServiceHandle(scManager);
    return service;
}

void ServiceManager::setLastError(const std::string& operation) {
    DWORD errorCode = GetLastError();
    std::ostringstream oss;
    oss << operation << " (Error " << errorCode << ")";
    
    // Get Windows error message
    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&messageBuffer), 0, nullptr);

    if (size > 0 && messageBuffer) {
        oss << ": " << messageBuffer;
        LocalFree(messageBuffer);
    }

    m_lastError = oss.str();
}

bool ServiceManager::waitForServiceState(SC_HANDLE serviceHandle, DWORD desiredState, DWORD timeoutMs) {
    SERVICE_STATUS status;
    DWORD startTime = GetTickCount();

    while (GetTickCount() - startTime < timeoutMs) {
        if (!QueryServiceStatus(serviceHandle, &status)) {
            return false;
        }

        if (status.dwCurrentState == desiredState) {
            return true;
        }

        Sleep(250); // Wait 250ms before checking again
    }

    return false; // Timeout
}

} // namespace burwell