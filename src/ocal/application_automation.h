#pragma once

// Windows headers must come first to avoid macro conflicts
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef ERROR  // Undefine the Windows ERROR macro to avoid conflicts
#endif

#include "window_management.h"
#include "../common/types.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace ocal {
namespace application {

// Configuration system removed - use atomic operations with nested scripts

// Enhanced window information with application context
struct EnhancedWindowInfo {
    window::WindowHandle handle;
    std::string title;
    std::string className;
    std::string applicationName;  // Detected application name
    window::Rectangle bounds;
    bool isVisible;
    bool isMinimized;
    bool isMaximized;
    unsigned long processId;
    bool isMainWindow;  // Alt+Tab window
};

// Enhanced window enumeration functions
std::vector<EnhancedWindowInfo> enumerateApplicationWindows(const std::string& appName);
std::vector<EnhancedWindowInfo> enumerateAllApplicationWindows();

// Configuration-dependent CPL functions removed - use atomic operations instead

// Universal automation functions (basic atomic utilities only)
namespace universal {
    #ifdef _WIN32
    
    // =============================================================================
    // ATOMIC UTILITY FUNCTIONS - BASIC OPERATIONS ONLY
    // =============================================================================
    // 
    // All complex automation functions have been removed in favor of atomic 
    // operations and nested scripts following the "Learn, Abstract, Reuse" philosophy.
    //
    // For complex operations, use the atomic operations available in orchestrator:
    // - UIA_FIND_WINDOWS_BY_CLASS, UIA_FIND_WINDOWS_BY_TITLE
    // - UIA_FOCUS_WINDOW, UIA_GET_WINDOW_INFO
    // - UIA_LAUNCH_APPLICATION, UIA_TERMINATE_PROCESS
    // - UIA_SHELL_EXECUTE, UIA_FIND_FILES_BY_PATTERN, UIA_FIND_FILES_BY_EXTENSION
    // - UIA_MOVE_FILES, UIA_DELETE_FILES, UIA_CREATE_DIRECTORY
    // - UIA_GET_FILE_INFO, UIA_KEY_PRESS, UIA_MOUSE_CLICK, etc.
    //
    // And nested scripts in test_scripts/:
    // - uia_find_windows_by_title.json, uia_open_file_explorer.json
    // - uia_bring_window_to_front.json, uia_clear_desktop.json
    // - uia_smart_file_organizer.json, uia_cleanup_downloads_folder.json
    // - And many more...
    //
    // =============================================================================
    
    /**
     * Get window title as wide string (atomic utility)
     * @param hwnd Window handle to query
     * @return Wide string containing window title
     */
    std::wstring GetWindowTitle(HWND hwnd);
    
    /**
     * Get window class name as wide string (atomic utility)
     * @param hwnd Window handle to query
     * @return Wide string containing class name
     */
    std::wstring GetWindowClassName(HWND hwnd);
    
    /**
     * Check if window is minimized (atomic utility)
     * @param hwnd Window handle to check
     * @return true if window is minimized, false otherwise
     */
    bool IsWindowMinimized(HWND hwnd);
    
    #endif
}

// Configuration-dependent functions removed - use atomic operations instead

} // namespace application
} // namespace ocal