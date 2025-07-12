#ifndef BURWELL_SERVICE_MANAGER_H
#define BURWELL_SERVICE_MANAGER_H

#include <string>
#include <windows.h>

namespace burwell {

/**
 * @brief Windows Service Manager for Burwell
 * 
 * Manages Burwell as a Windows service with install, uninstall,
 * start, stop, and status operations.
 */
class ServiceManager {
public:
    ServiceManager();
    ~ServiceManager();

    /**
     * @brief Install Burwell as a Windows service
     * @param burwellPath Path to burwell.exe
     * @param serviceName Name of the service (default: "BurwellAgent")
     * @param displayName Display name for the service
     * @param description Service description
     * @return true if successful, false otherwise
     */
    bool installService(const std::string& burwellPath,
                       const std::string& serviceName = "BurwellAgent",
                       const std::string& displayName = "Burwell AI Desktop Agent",
                       const std::string& description = "AI-powered desktop automation agent");

    /**
     * @brief Uninstall Burwell service
     * @param serviceName Name of the service to uninstall
     * @return true if successful, false otherwise
     */
    bool uninstallService(const std::string& serviceName = "BurwellAgent");

    /**
     * @brief Start the Burwell service
     * @param serviceName Name of the service to start
     * @return true if successful, false otherwise
     */
    bool startService(const std::string& serviceName = "BurwellAgent");

    /**
     * @brief Stop the Burwell service
     * @param serviceName Name of the service to stop
     * @return true if successful, false otherwise
     */
    bool stopService(const std::string& serviceName = "BurwellAgent");

    /**
     * @brief Check service status
     * @param serviceName Name of the service to check
     * @return Service status string
     */
    std::string getServiceStatus(const std::string& serviceName = "BurwellAgent");

    /**
     * @brief Check if service is installed
     * @param serviceName Name of the service to check
     * @return true if installed, false otherwise
     */
    bool isServiceInstalled(const std::string& serviceName = "BurwellAgent");

    /**
     * @brief Get the last error message
     * @return Error message string
     */
    std::string getLastError() const { return m_lastError; }

private:
    /**
     * @brief Open Service Control Manager
     * @param access Desired access rights
     * @return Handle to SCM or nullptr on failure
     */
    SC_HANDLE openSCManager(DWORD access = SC_MANAGER_ALL_ACCESS);

    /**
     * @brief Open service handle
     * @param serviceName Name of the service
     * @param access Desired access rights
     * @return Handle to service or nullptr on failure
     */
    SC_HANDLE openService(const std::string& serviceName, DWORD access = SERVICE_ALL_ACCESS);

    /**
     * @brief Set the last error message
     * @param operation Operation that failed
     */
    void setLastError(const std::string& operation);

    /**
     * @brief Wait for service to reach desired state
     * @param serviceHandle Handle to the service
     * @param desiredState State to wait for
     * @param timeoutMs Timeout in milliseconds
     * @return true if successful, false on timeout or error
     */
    bool waitForServiceState(SC_HANDLE serviceHandle, DWORD desiredState, DWORD timeoutMs = 30000);

    std::string m_lastError;
};

} // namespace burwell

#endif // BURWELL_SERVICE_MANAGER_H