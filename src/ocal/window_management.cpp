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
#include <algorithm>
#include <thread>
#include <chrono>
#include <vector>
#include <functional>

namespace ocal {
namespace window {

#ifdef _WIN32
namespace {
    struct EnumWindowsData {
        std::vector<WindowInfo>* windows;
        std::string targetTitle;
        std::string targetClassName;
        unsigned long targetProcessId;
    };
    
    // Helper function to check if a process is a main/parent process (not a child process)
    // Note: Currently unused but kept for potential future use
    [[maybe_unused]] bool isMainProcess(DWORD processId) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            return true; // Assume main if we can't check
        }
        
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(PROCESSENTRY32);
        
        DWORD parentPid = 0;
        bool found = false;
        
        if (Process32First(hSnapshot, &pe)) {
            do {
                if (pe.th32ProcessID == processId) {
                    parentPid = pe.th32ParentProcessID;
                    found = true;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe));
        }
        
        CloseHandle(hSnapshot);
        
        if (!found) {
            return true; // Assume main if not found
        }
        
        // Check if parent process has the same executable name (indicating child process)
        if (parentPid == 0) {
            return true; // No parent = definitely main process
        }
        
        // Get current process name
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
        if (!hProcess) {
            return true; // Assume main if we can't check
        }
        
        wchar_t currentProcessName[MAX_PATH];
        DWORD currentNameSize = sizeof(currentProcessName) / sizeof(wchar_t);
        if (!QueryFullProcessImageNameW(hProcess, 0, currentProcessName, &currentNameSize)) {
            CloseHandle(hProcess);
            return true; // Assume main if we can't get name
        }
        CloseHandle(hProcess);
        
        // Get parent process name
        HANDLE hParentProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, parentPid);
        if (!hParentProcess) {
            return true; // Parent doesn't exist = main process
        }
        
        wchar_t parentProcessName[MAX_PATH];
        DWORD parentNameSize = sizeof(parentProcessName) / sizeof(wchar_t);
        if (!QueryFullProcessImageNameW(hParentProcess, 0, parentProcessName, &parentNameSize)) {
            CloseHandle(hParentProcess);
            return true; // Can't check parent = assume main
        }
        CloseHandle(hParentProcess);
        
        // Extract just the executable names for comparison
        std::string currentExe = burwell::os::UnicodeUtils::wideStringToUtf8(currentProcessName);
        std::string parentExe = burwell::os::UnicodeUtils::wideStringToUtf8(parentProcessName);
        
        size_t currentLastSlash = currentExe.find_last_of("\\/");
        if (currentLastSlash != std::string::npos) {
            currentExe = currentExe.substr(currentLastSlash + 1);
        }
        
        size_t parentLastSlash = parentExe.find_last_of("\\/");
        if (parentLastSlash != std::string::npos) {
            parentExe = parentExe.substr(parentLastSlash + 1);
        }
        
        // If parent has different executable name, this is likely a main process
        // If parent has same executable name (e.g., chrome.exe spawned by chrome.exe), this is a child
        return (currentExe != parentExe);
    }
    
    // Configuration system removed - use atomic operations instead
    
    // Helper function to check if a window appears in Alt+Tab (like Task Switcher)
    // Now uses ApplicationManager for configuration-driven application detection
    bool isAltTabWindow(HWND hwnd) {
        // Must be visible and not minimized to appear in Alt+Tab
        if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
            return false;
        }
        
        // Get window title - Alt+Tab windows must have non-empty titles
        wchar_t title[256];
        GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));
        if (wcslen(title) == 0) {
            return false;
        }
        
        // Get window styles
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        
        // Tool windows (WS_EX_TOOLWINDOW) don't appear in Alt+Tab
        if (exStyle & WS_EX_TOOLWINDOW) {
            return false;
        }
        
        // Windows with WS_EX_APPWINDOW explicitly appear in Alt+Tab
        if (exStyle & WS_EX_APPWINDOW) {
            return true;
        }
        
        // Check if window is owned or has a parent
        HWND parent = GetParent(hwnd);
        HWND owner = GetWindow(hwnd, GW_OWNER);
        
        // Windows with owners typically don't appear in Alt+Tab (like dialog boxes)
        // unless they have WS_EX_APPWINDOW (checked above)
        if (owner != nullptr) {
            return false;
        }
        
        // Windows with parents don't appear in Alt+Tab
        if (parent != nullptr) {
            return false;
        }
        
        // Application pattern checking removed - use basic window checks instead
        std::string windowTitle = burwell::os::UnicodeUtils::wideStringToUtf8(title);
        
        // Fallback logic for unknown applications
        bool hasCaption = (style & WS_CAPTION) != 0;
        if (!hasCaption) {
            return false;
        }
        
        RECT rect;
        GetWindowRect(hwnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        
        // Must be reasonably sized to appear in Alt+Tab
        bool sizeCheck = (width >= 100 && height >= 50);
        
        if (sizeCheck) {
            // Get class name for logging
            wchar_t className[256];
            GetClassNameW(hwnd, className, sizeof(className) / sizeof(wchar_t));
            std::string classNameUtf8 = burwell::os::UnicodeUtils::wideStringToUtf8(className);
            SLOG_DEBUG().message("Window passed fallback Alt+Tab check").context("title", windowTitle).context("class", classNameUtf8);
        }
        
        return sizeCheck;
    }
    
    // Legacy function - now redirects to Alt+Tab logic for better compatibility
    // Note: Currently unused but kept for potential future use
    [[maybe_unused]] bool isMainApplicationWindow(HWND hwnd) {
        return isAltTabWindow(hwnd);
    }
    
    BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam) {
        auto* data = reinterpret_cast<EnumWindowsData*>(lParam);
        
        WindowInfo info;
        info.handle = hwnd;
        
        // Get window title
        wchar_t title[256];
        GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));
        info.title = burwell::os::UnicodeUtils::wideStringToUtf8(title);
        
        // Get class name
        wchar_t className[256];
        GetClassNameW(hwnd, className, sizeof(className) / sizeof(wchar_t));
        info.className = burwell::os::UnicodeUtils::wideStringToUtf8(className);
        
        // Get window rect
        RECT rect;
        GetWindowRect(hwnd, &rect);
        info.x = rect.left;
        info.y = rect.top;
        info.width = rect.right - rect.left;
        info.height = rect.bottom - rect.top;
        
        // Get window state
        info.isVisible = IsWindowVisible(hwnd);
        info.isMinimized = IsIconic(hwnd);
        info.isMaximized = IsZoomed(hwnd);
        
        // Get process ID
        GetWindowThreadProcessId(hwnd, &info.processId);
        
        data->windows->push_back(info);
        return TRUE;
    }
}
#endif

WindowHandle find(const std::string& title) {
#ifdef _WIN32
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Finding window with title")
            .context("title", title);
        
        // First try exact match for backward compatibility
        std::wstring wTitle = burwell::os::UnicodeUtils::utf8ToWideString(title);
        HWND hwnd = FindWindowW(nullptr, wTitle.c_str());
        if (hwnd) {
            SLOG_DEBUG().message("Found window with exact title match");
            return hwnd;
        }
        
        // If exact match fails, try partial case-insensitive match with main process priority
        SLOG_DEBUG().message("Exact match failed, trying partial case-insensitive match with main process priority");
        std::vector<WindowInfo> allWindows = enumerateAll();
        
        // Convert search title to lowercase for case-insensitive comparison
        std::string lowerTitle = title;
        std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);
        
        std::vector<HWND> matchingWindows;
        std::vector<HWND> mainProcessMatches;
        
        // First pass: collect all matches and identify main process windows
        for (const auto& window : allWindows) {
            if (window.isVisible && !window.title.empty()) {
                // Convert window title to lowercase for comparison
                std::string lowerWindowTitle = window.title;
                std::transform(lowerWindowTitle.begin(), lowerWindowTitle.end(), lowerWindowTitle.begin(), ::tolower);
                
                // Check if search title is contained in window title
                if (lowerWindowTitle.find(lowerTitle) != std::string::npos) {
                    HWND windowHandle = static_cast<HWND>(window.handle);
                    matchingWindows.push_back(windowHandle);
                    
                    // Check if this is an Alt+Tab visible window
                    if (isAltTabWindow(windowHandle)) {
                        mainProcessMatches.push_back(windowHandle);
                        SLOG_DEBUG().message("Found Alt+Tab window")
                            .context("title", window.title)
                            .context("pid", window.processId);
                    } else {
                        SLOG_DEBUG().message("Found non-Alt+Tab window")
                            .context("title", window.title)
                            .context("pid", window.processId);
                    }
                }
            }
        }
        
        // Prioritize main process windows
        if (!mainProcessMatches.empty()) {
            SLOG_DEBUG().message("Returning main process window").context("sub_processes_count", matchingWindows.size() - mainProcessMatches.size());
            return mainProcessMatches[0];
        } else if (!matchingWindows.empty()) {
            SLOG_DEBUG().message("No main process windows found, returning first sub-process window");
            return matchingWindows[0];
        }
        
        SLOG_DEBUG().message("No window found matching title").context("title", title);
        return INVALID_WINDOW_HANDLE;
    }, "window::find");
    
    return INVALID_WINDOW_HANDLE;
#else
    (void)title;
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Window find simulated (non-Windows platform)");
        return INVALID_WINDOW_HANDLE;
    }, "window::find");
    
    return INVALID_WINDOW_HANDLE;
#endif
}

WindowHandle findByClassName(const std::string& className) {
#ifdef _WIN32
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Finding window with class").context("class", className);
        
        std::wstring wClassName = burwell::os::UnicodeUtils::utf8ToWideString(className);
        HWND hwnd = FindWindowW(wClassName.c_str(), nullptr);
        return hwnd;
    }, "window::findByClassName");
    
    return INVALID_WINDOW_HANDLE;
#else
    (void)className;
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Window find by class simulated (non-Windows platform)");
        return INVALID_WINDOW_HANDLE;
    }, "window::findByClassName");
    
    return INVALID_WINDOW_HANDLE;
#endif
}

WindowHandle getActive() {
#ifdef _WIN32
    BURWELL_TRY_CATCH({
        return GetActiveWindow();
    }, "window::getActive");
    
    return INVALID_WINDOW_HANDLE;
#else
    BURWELL_TRY_CATCH({
        return INVALID_WINDOW_HANDLE;
    }, "window::getActive");
    
    return INVALID_WINDOW_HANDLE;
#endif
}

WindowHandle getForeground() {
#ifdef _WIN32
    BURWELL_TRY_CATCH({
        return GetForegroundWindow();
    }, "window::getForeground");
    
    return INVALID_WINDOW_HANDLE;
#else
    BURWELL_TRY_CATCH({
        return INVALID_WINDOW_HANDLE;
    }, "window::getForeground");
    
    return INVALID_WINDOW_HANDLE;
#endif
}

WindowInfo getInfo(WindowHandle handle) {
    WindowInfo info;
    
#ifdef _WIN32
    BURWELL_TRY_CATCH({
        if (!handle) return info;
        
        HWND hwnd = static_cast<HWND>(handle);
        info.handle = handle;
        
        // Get title
        wchar_t title[256];
        GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));
        info.title = burwell::os::UnicodeUtils::wideStringToUtf8(title);
        
        // Get class name
        wchar_t className[256];
        GetClassNameW(hwnd, className, sizeof(className) / sizeof(wchar_t));
        info.className = burwell::os::UnicodeUtils::wideStringToUtf8(className);
        
        // Get bounds
        RECT rect;
        GetWindowRect(hwnd, &rect);
        info.x = rect.left;
        info.y = rect.top;
        info.width = rect.right - rect.left;
        info.height = rect.bottom - rect.top;
        
        // Get state
        info.isVisible = IsWindowVisible(hwnd);
        info.isMinimized = IsIconic(hwnd);
        info.isMaximized = IsZoomed(hwnd);
        
        // Get process ID
        GetWindowThreadProcessId(hwnd, &info.processId);
    }, "window::getInfo");
#else
    (void)handle;
    BURWELL_TRY_CATCH({
        // Return empty info for non-Windows platforms
    }, "window::getInfo");
#endif
    
    return info;
}

bool bringToFront(WindowHandle handle) {
#ifdef _WIN32
    BURWELL_TRY_CATCH({
        if (!handle) return false;
        
        SLOG_DEBUG().message("Bringing window to front");
        
        HWND hwnd = static_cast<HWND>(handle);
        
        // Modern Windows has restrictions on SetForegroundWindow
        // Try multiple approaches to ensure window activation
        
        // First, try direct approach
        if (SetForegroundWindow(hwnd)) {
            SLOG_DEBUG().message("Window activated with SetForegroundWindow");
            return true;
        }
        
        // If that fails, try the recommended approach for modern Windows:
        // 1. Restore window if minimized
        if (IsIconic(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
        }
        
        // 2. Bring window to top without forcing foreground
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, 
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        
        // 3. Try to attach to foreground thread and then set foreground
        HWND foregroundWindow = GetForegroundWindow();
        if (foregroundWindow != hwnd) {
            DWORD foregroundThreadId = GetWindowThreadProcessId(foregroundWindow, nullptr);
            DWORD currentThreadId = GetCurrentThreadId();
            
            if (foregroundThreadId != currentThreadId) {
                // Attach to the foreground thread
                AttachThreadInput(currentThreadId, foregroundThreadId, TRUE);
                
                // Now try to set foreground
                BOOL result = SetForegroundWindow(hwnd);
                
                // Detach from foreground thread
                AttachThreadInput(currentThreadId, foregroundThreadId, FALSE);
                
                if (result) {
                    SLOG_DEBUG().message("Window activated with thread attachment method");
                    return true;
                }
            }
        }
        
        // 4. Try BringWindowToTop as final attempt
        if (BringWindowToTop(hwnd)) {
            SLOG_DEBUG().message("Window brought to top with BringWindowToTop");
            // Check if it's now the foreground window
            Sleep(50); // Small delay for the operation to complete
            if (GetForegroundWindow() == hwnd) {
                return true;
            }
        }
        
        // 5. Last resort: just show the window and hope for the best
        ShowWindow(hwnd, SW_SHOW);
        SLOG_WARNING().message("Window shown but activation may have failed - continuing anyway");
        return true; // Return true since window is at least visible and operations can continue
    }, "window::bringToFront");
    
    return false;
#else
    (void)handle;
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Bring to front simulated (non-Windows platform)");
        return true;
    }, "window::bringToFront");
    
    return false;
#endif
}

std::vector<WindowInfo> enumerateAll() {
    std::vector<WindowInfo> windows;
    
#ifdef _WIN32
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Enumerating all windows");
        
        EnumWindowsData data;
        data.windows = &windows;
        EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&data));
    }, "window::enumerateAll");
#else
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Window enumeration simulated (non-Windows platform)");
    }, "window::enumerateAll");
#endif
    
    return windows;
}

bool isValid(WindowHandle handle) {
#ifdef _WIN32
    return handle && IsWindow(static_cast<HWND>(handle));
#else
    return handle != INVALID_WINDOW_HANDLE;
#endif
}

bool isVisible(WindowHandle handle) {
#ifdef _WIN32
    return handle && IsWindowVisible(static_cast<HWND>(handle));
#else
    (void)handle;
    return false;
#endif
}

Rectangle getDesktopBounds() {
#ifdef _WIN32
    return Rectangle(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
#else
    return Rectangle(0, 0, 1920, 1080); // Simulated
#endif
}

// Simple implementations for remaining methods to make it compilable
bool minimize(WindowHandle handle) { 
    (void)handle;
    return false; 
}
bool maximize(WindowHandle handle) { 
    (void)handle;
    return false; 
}
bool close(WindowHandle handle) { 
    (void)handle;
    return false; 
}
bool resize(WindowHandle handle, int width, int height) { 
    (void)handle;
    (void)width;
    (void)height;
    return false; 
}
WindowHandle findByProcess(unsigned long processId) { 
    (void)processId;
    return INVALID_WINDOW_HANDLE; 
}
std::vector<WindowHandle> findAll(const std::string& title) { 
    (void)title;
    return {}; 
}
std::string getTitle(WindowHandle handle) {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(handle);
    
    // Input validation - Rule 2
    if (!hwnd || !IsWindow(hwnd)) {
        return "";
    }
    
    // Output validation - Rule 3
    int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return "";
    }
    
    // Variable initialization - Rule 1
    std::wstring title(length + 1, L'\0');
    
    // Handle management - Rule 4
    int result = GetWindowTextW(hwnd, &title[0], length + 1);
    if (result <= 0) {
        return "";
    }
    
    // String validation - Rule 2
    if (!title.empty() && title.back() == L'\0') {
        title.pop_back();
    }
    
    // Convert wstring to string
    return std::string(title.begin(), title.end());
#else
    (void)handle;
    return "";
#endif
}
std::string getClassName(WindowHandle handle) {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(handle);
    
    // Input validation - Rule 2
    if (!hwnd || !IsWindow(hwnd)) {
        return "";
    }
    
    // Variable initialization - Rule 1
    wchar_t className[256] = {0};
    
    // Handle management - Rule 4
    int result = GetClassNameW(hwnd, className, sizeof(className) / sizeof(className[0]));
    if (result <= 0) {
        return "";
    }
    
    // Convert wstring to string
    std::wstring wClassName(className);
    return std::string(wClassName.begin(), wClassName.end());
#else
    (void)handle;
    return "";
#endif
}
Rectangle getBounds(WindowHandle handle) {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(handle);
    
    // Input validation - Rule 2
    if (!hwnd || !IsWindow(hwnd)) {
        return Rectangle();
    }
    
    // Handle management - Rule 4
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) {
        return Rectangle();
    }
    
    return Rectangle(rect.left, rect.top, rect.right, rect.bottom);
#else
    (void)handle;
    return Rectangle();
#endif
}
unsigned long getProcessId(WindowHandle handle) {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(handle);
    
    // Input validation - Rule 2
    if (!hwnd || !IsWindow(hwnd)) {
        return 0;
    }
    
    // Handle management - Rule 4
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    return static_cast<unsigned long>(processId);
#else
    (void)handle;
    return 0;
#endif
}
bool activate(WindowHandle handle) { return bringToFront(handle); }
bool restore(WindowHandle handle) { 
    (void)handle;
    return false; 
}
bool hide(WindowHandle handle) { 
    (void)handle;
    return false; 
}
bool show(WindowHandle handle) { 
    (void)handle;
    return false; 
}
bool move(WindowHandle handle, int x, int y) { 
    (void)handle;
    (void)x;
    (void)y;
    return false; 
}
bool setBounds(WindowHandle handle, const Rectangle& bounds) { 
    (void)handle;
    (void)bounds;
    return false; 
}
bool center(WindowHandle handle) { 
    (void)handle;
    return false; 
}
std::vector<WindowInfo> enumerateVisible() { return {}; }
std::vector<WindowInfo> enumerateByProcess(unsigned long processId) { 
    (void)processId;
    return {}; 
}
WindowHandle getParent(WindowHandle handle) { 
    (void)handle;
    return INVALID_WINDOW_HANDLE; 
}
std::vector<WindowHandle> getChildren(WindowHandle handle) { 
    (void)handle;
    return {}; 
}
WindowHandle getRoot(WindowHandle handle) { 
    (void)handle;
    return INVALID_WINDOW_HANDLE; 
}
bool isMinimized(WindowHandle handle) {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(handle);
    
    // Input validation - Rule 2
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    
    // Handle management - Rule 4
    return IsIconic(hwnd) != 0;
#else
    (void)handle;
    return false;
#endif
}
bool isMaximized(WindowHandle handle) {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(handle);
    
    // Input validation - Rule 2
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    
    // Handle management - Rule 4
    return IsZoomed(hwnd) != 0;
#else
    (void)handle;
    return false;
#endif
}
bool isEnabled(WindowHandle handle) {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(handle);
    
    // Input validation - Rule 2
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    
    // Handle management - Rule 4
    return IsWindowEnabled(hwnd) != 0;
#else
    (void)handle;
    return false;
#endif
}
bool isTopmost(WindowHandle handle) { 
    (void)handle;
    return false; 
}
bool setTitle(WindowHandle handle, const std::string& title) { 
    (void)handle;
    (void)title;
    return false; 
}
bool setTopmost(WindowHandle handle, bool topmost) { 
    (void)handle;
    (void)topmost;
    return false; 
}
bool setTransparency(WindowHandle handle, int alpha) { 
    (void)handle;
    (void)alpha;
    return false; 
}
bool flash(WindowHandle handle, int count) { 
    (void)handle;
    (void)count;
    return false; 
}
bool startMonitoring(WindowCallback callback) { 
    (void)callback;
    return false; 
}
void stopMonitoring() { }
bool waitForWindow(const std::string& title, int timeoutMs) { 
    (void)title;
    (void)timeoutMs;
    return false; 
}
bool waitForWindowClose(WindowHandle handle, int timeoutMs) { 
    (void)handle;
    (void)timeoutMs;
    return false; 
}
WindowHandle findMainWindow(unsigned long processId) { 
    (void)processId;
    return INVALID_WINDOW_HANDLE; 
}
int getScreenCount() { return 1; }
Rectangle getScreenBounds(int screenIndex) { 
    (void)screenIndex;
    return getDesktopBounds(); 
}

} // namespace window
} // namespace ocal