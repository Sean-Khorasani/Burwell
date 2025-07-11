#ifndef BURWELL_OCAL_H
#define BURWELL_OCAL_H

#include "mouse_control.h"
#include "keyboard_control.h"
#include "window_management.h"
#include "application_automation.h"
#include "service_management.h"
#include "system_command.h"
#include "filesystem_operations.h"

namespace burwell {

// Wrapper classes that provide object-oriented interface to namespace functions
class MouseControl {
public:
    bool click(int x, int y, const std::string& button = "left");
    bool moveTo(int x, int y);
    bool drag(int x1, int y1, int x2, int y2);
    std::pair<int, int> getCurrentPosition();
};

class KeyboardControl {
public:
    bool typeText(const std::string& text);
    bool sendHotkey(const std::vector<std::string>& keys);
    bool pressKey(const std::string& key);
};

class WindowManagement {
public:
    void* findWindow(const std::string& title);
    bool activateWindow(void* handle);
    bool resizeWindow(void* handle, int width, int height);
};

class AppManagement {
public:
    bool launchApplication(const std::string& appName);
    bool closeApplication(const std::string& appName);
    unsigned long getProcessId(const std::string& appName);
};

class ServiceManagement {
public:
    bool startService(const std::string& serviceName);
    bool stopService(const std::string& serviceName);
    bool restartService(const std::string& serviceName);
};

class SystemCommand {
public:
    struct CommandResult {
        bool success;
        int exitCode;
        std::string output;
        std::string error;
        std::string command;
        
        CommandResult() : success(false), exitCode(-1) {}
    };
    
    CommandResult executeCommand(const std::string& command, 
                                const std::string& workingDir = "",
                                const std::map<std::string, std::string>& environment = {},
                                int timeoutMs = 30000,
                                bool captureOutput = true,
                                bool showWindow = false);
};

class FilesystemOperations {
public:
    bool fileExists(const std::string& path);
    bool directoryExists(const std::string& path);
    bool copyFile(const std::string& source, const std::string& destination, bool overwrite = false);
    bool copyDirectory(const std::string& source, const std::string& destination, bool recursive = false, bool overwrite = false);
    bool deleteFile(const std::string& path);
    bool deleteDirectory(const std::string& path, bool recursive = false);
    bool createDirectory(const std::string& path);
    bool waitForFile(const std::string& path, int timeoutMs = 60000, int checkIntervalMs = 1000);
    
    struct FileInfo {
        bool exists;
        bool isDirectory;
        size_t size;
        std::time_t lastModified;
        std::string fullPath;
    };
    
    FileInfo getFileInfo(const std::string& path);
};

// The OCAL (OS Control Abstraction Layer) class provides a unified interface
// to all operating system control functionalities
class OCAL {
public:
    OCAL();
    ~OCAL();
    
    // Access to control modules
    MouseControl& mouseControl() { return m_mouseControl; }
    KeyboardControl& keyboardControl() { return m_keyboardControl; }
    WindowManagement& windowManagement() { return m_windowManagement; }
    AppManagement& appManagement() { return m_appManagement; }
    ServiceManagement& serviceManagement() { return m_serviceManagement; }
    SystemCommand& systemCommand() { return m_systemCommand; }
    FilesystemOperations& filesystemOperations() { return m_filesystemOperations; }
    
    // Convenience methods
    bool initialize();
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    
private:
    MouseControl m_mouseControl;
    KeyboardControl m_keyboardControl;
    WindowManagement m_windowManagement;
    AppManagement m_appManagement;
    ServiceManagement m_serviceManagement;
    SystemCommand m_systemCommand;
    FilesystemOperations m_filesystemOperations;
    bool m_initialized;
};

} // namespace burwell

#endif // BURWELL_OCAL_H


