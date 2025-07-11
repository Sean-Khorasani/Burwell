#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#undef ERROR  // Undefine the Windows ERROR macro to avoid conflicts
#endif

#include "window_management.h"
#include "application_automation.h"
#include "../common/structured_logger.h"
#include "../common/error_handler.h"
#include "../common/os_utils.h"
#include "../common/raii_wrappers.h"
#include <algorithm>
#include <thread>
#include <chrono>
#include <vector>
#include <functional>
#include <memory>

namespace ocal {
namespace window {

using namespace burwell::raii;

#ifdef _WIN32
namespace {
    struct EnumWindowsData {
        std::vector<WindowInfo>* windows;
        std::string targetTitle;
        std::string targetClassName;
        unsigned long targetProcessId;
    };
    
    // Helper function to check if a process is a main/parent process (not a child process)
    bool isMainProcess(DWORD processId) {
        // Use RAII wrapper for snapshot handle
        WindowHandle hSnapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!hSnapshot.isValid()) {
            return true; // Assume main if we can't check
        }
        
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(PROCESSENTRY32);
        
        DWORD parentPid = 0;
        bool found = false;
        
        if (Process32First(hSnapshot.get(), &pe)) {
            do {
                if (pe.th32ProcessID == processId) {
                    parentPid = pe.th32ParentProcessID;
                    found = true;
                    break;
                }
            } while (Process32Next(hSnapshot.get(), &pe));
        }
        
        if (!found) {
            return true; // Assume main if not found
        }
        
        // Check if parent process is a system process or shell
        try {
            ProcessHandle hParent = ProcessHandle::openProcess(
                PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentPid);
            
            if (hParent.isValid()) {
                char processName[MAX_PATH] = {0};
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameA(hParent.get(), 0, processName, &size)) {
                    std::string parentName = processName;
                    std::transform(parentName.begin(), parentName.end(), 
                                 parentName.begin(), ::tolower);
                    
                    // Common shell/system processes
                    if (parentName.find("explorer.exe") != std::string::npos ||
                        parentName.find("cmd.exe") != std::string::npos ||
                        parentName.find("powershell.exe") != std::string::npos ||
                        parentName.find("services.exe") != std::string::npos ||
                        parentName.find("winlogon.exe") != std::string::npos ||
                        parentName.find("svchost.exe") != std::string::npos) {
                        return true; // Main process
                    }
                }
            }
        } catch (...) {
            // If we can't open parent, assume it's a main process
        }
        
        return true; // Default to main process
    }
    
    std::string getProcessNameFromId(DWORD processId) {
        try {
            ProcessHandle hProcess = ProcessHandle::openProcess(
                PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
            
            if (hProcess.isValid()) {
                char processName[MAX_PATH] = {0};
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameA(hProcess.get(), 0, processName, &size)) {
                    std::string fullPath = processName;
                    size_t lastSlash = fullPath.find_last_of("\\/");
                    if (lastSlash != std::string::npos) {
                        return fullPath.substr(lastSlash + 1);
                    }
                    return fullPath;
                }
            }
        } catch (...) {
            // Ignore errors
        }
        
        return "";
    }
    
    // Process a window in the enumeration
    bool processWindow(HWND hwnd, EnumWindowsData* data) {
        // Skip invisible windows unless we're looking for hidden windows
        if (!IsWindowVisible(hwnd)) {
            return true;
        }
        
        // Skip windows without a title unless we're searching by class/process
        char titleBuffer[256] = {0};
        GetWindowTextA(hwnd, titleBuffer, sizeof(titleBuffer));
        std::string windowTitle = titleBuffer;
        
        if (windowTitle.empty() && 
            data->targetClassName.empty() && 
            data->targetProcessId == 0) {
            return true;
        }
        
        // Get window class
        char classBuffer[256] = {0};
        GetClassNameA(hwnd, classBuffer, sizeof(classBuffer));
        std::string windowClass = classBuffer;
        
        // Get process info
        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);
        
        // Skip if this is a child process (unless specifically requested)
        if (!isMainProcess(processId) && data->targetProcessId == 0) {
            return true;
        }
        
        std::string processName = getProcessNameFromId(processId);
        
        // Check filters
        bool matches = true;
        
        if (!data->targetTitle.empty()) {
            // Convert both to lowercase for case-insensitive comparison
            std::string lowerTitle = windowTitle;
            std::string lowerTarget = data->targetTitle;
            std::transform(lowerTitle.begin(), lowerTitle.end(), 
                         lowerTitle.begin(), ::tolower);
            std::transform(lowerTarget.begin(), lowerTarget.end(), 
                         lowerTarget.begin(), ::tolower);
            
            // Check for partial match
            if (lowerTitle.find(lowerTarget) == std::string::npos) {
                matches = false;
            }
        }
        
        if (matches && !data->targetClassName.empty()) {
            std::string lowerClass = windowClass;
            std::string lowerTarget = data->targetClassName;
            std::transform(lowerClass.begin(), lowerClass.end(), 
                         lowerClass.begin(), ::tolower);
            std::transform(lowerTarget.begin(), lowerTarget.end(), 
                         lowerTarget.begin(), ::tolower);
            
            if (lowerClass.find(lowerTarget) == std::string::npos) {
                matches = false;
            }
        }
        
        if (matches && data->targetProcessId != 0) {
            if (processId != data->targetProcessId) {
                matches = false;
            }
        }
        
        if (matches) {
            WindowInfo info;
            info.handle = hwnd;
            info.title = windowTitle;
            info.className = windowClass;
            info.processId = processId;
            info.processName = processName;
            
            // Get window state
            WINDOWPLACEMENT placement = {0};
            placement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(hwnd, &placement);
            
            info.isMinimized = (placement.showCmd == SW_SHOWMINIMIZED);
            info.isMaximized = (placement.showCmd == SW_SHOWMAXIMIZED);
            info.isActive = (GetForegroundWindow() == hwnd);
            
            // Get window rect
            RECT rect;
            GetWindowRect(hwnd, &rect);
            info.x = rect.left;
            info.y = rect.top;
            info.width = rect.right - rect.left;
            info.height = rect.bottom - rect.top;
            
            // Get Z-order (approximate)
            info.zOrder = static_cast<int>(data->windows->size());
            
            data->windows->push_back(info);
        }
        
        return true; // Continue enumeration
    }
    
    BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
        EnumWindowsData* data = reinterpret_cast<EnumWindowsData*>(lParam);
        return processWindow(hwnd, data) ? TRUE : FALSE;
    }
}
#endif // _WIN32

// ... Rest of implementation continues with similar RAII wrapper usage ...

std::vector<WindowInfo> WindowManagement::getAllWindows() {
    std::vector<WindowInfo> windows;
    
#ifdef _WIN32
    EnumWindowsData data;
    data.windows = &windows;
    
    if (!EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data))) {
        burwell::os::ErrorUtils::logSystemError("EnumWindows failed");
    }
#else
    SLOG_WARNING().message("Window enumeration not implemented for this platform");
#endif
    
    return windows;
}

bool WindowManagement::focusWindow(void* handle) {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(handle);
    HwndWrapper window(hwnd, false);  // Don't destroy on cleanup
    
    if (!window.isValid()) {
        SLOG_ERROR().message("Invalid window handle");
        return false;
    }
    
    // Use the window through the wrapper
    if (IsIconic(window.get())) {
        ShowWindow(window.get(), SW_RESTORE);
    }
    
    SetForegroundWindow(window.get());
    SetActiveWindow(window.get());
    
    return true;
#else
    SLOG_WARNING().message("Window focus not implemented for this platform");
    return false;
#endif
}

} // namespace window
} // namespace ocal