#include "application_automation.h"
#include "../common/structured_logger.h"
#include "../common/error_handler.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

// Include JSON parsing (if available) or implement simple JSON parser
#include <sstream>

// Additional includes for verified file management functions
#include <random>
#include <filesystem>

#ifdef _WIN32
#include <shlobj.h>
#endif

namespace ocal {
namespace application {

// nlohmann::json is included at the top

// ApplicationManager class removed - use atomic operations instead

// Configuration parsing removed

// ApplicationManager methods removed

// Window pattern matching removed

// Title matching removed

// Alt+Tab window detection removed

// All remaining ApplicationManager methods removed

// Configuration-dependent functions removed - use atomic operations instead

// Configuration-dependent enumeration removed

// Configuration-dependent compatibility functions removed

// Configuration-dependent CPL functions removed

// More configuration-dependent functions removed

// Universal automation implementation (from tested UIA code)
// Application-agnostic functions that work with any browser/application
namespace universal {
#ifdef _WIN32

// 1. Window Detection & Management Functions (CRASH-SAFE)
std::wstring GetWindowTitle(HWND hwnd) {
    // Input validation - Rule 2
    if (!hwnd || !IsWindow(hwnd)) {
        return L"";
    }
    
    // Output validation - Rule 3
    int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return L"";
    }
    
    // Variable initialization - Rule 1
    std::wstring title(length + 1, L'\0');
    
    // Handle management - Rule 4
    int result = GetWindowTextW(hwnd, &title[0], length + 1);
    if (result <= 0) {
        return L"";
    }
    
    // String validation - Rule 2
    if (!title.empty() && title.back() == L'\0') {
        title.pop_back();
    }
    
    return title;
}

std::wstring GetWindowClassName(HWND hwnd) {
    // Input validation - Rule 2
    if (!hwnd || !IsWindow(hwnd)) {
        return L"";
    }
    
    // Variable initialization - Rule 1
    wchar_t className[256] = {0};
    
    // Handle management - Rule 4
    int result = GetClassNameW(hwnd, className, sizeof(className) / sizeof(className[0]));
    if (result <= 0) {
        return L"";
    }
    
    // String validation - Rule 2
    return std::wstring(className);
}

bool IsWindowMinimized(HWND hwnd) {
    // Input validation - Rule 2
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    
    // Handle management - Rule 4
    return IsIconic(hwnd);
}

// =============================================================================
// HARDCODED FUNCTIONS REMOVED - USE ATOMIC OPERATIONS AND NESTED SCRIPTS
// =============================================================================
//
// All complex automation functions have been removed in favor of atomic operations
// and nested scripts following the "Learn, Abstract, Reuse" philosophy.
//
// ATOMIC OPERATIONS AVAILABLE:
// - UIA_FIND_WINDOWS_BY_CLASS, UIA_FIND_WINDOWS_BY_TITLE
// - UIA_FOCUS_WINDOW, UIA_GET_WINDOW_INFO
// - UIA_LAUNCH_APPLICATION, UIA_TERMINATE_PROCESS
// - UIA_SHELL_EXECUTE, UIA_FIND_FILES_BY_PATTERN, UIA_FIND_FILES_BY_EXTENSION
// - UIA_MOVE_FILES, UIA_DELETE_FILES, UIA_CREATE_DIRECTORY
// - UIA_GET_FILE_INFO, UIA_KEY_PRESS, UIA_MOUSE_CLICK, etc.
//
// NESTED SCRIPTS AVAILABLE (in test_scripts/):
// - uia_find_windows_by_title.json, uia_find_windows_by_class.json
// - uia_bring_window_to_front.json, uia_aggressive_window_focus.json
// - uia_open_file_explorer.json, uia_clear_desktop.json
// - uia_launch_process.json, uia_wait_for_window.json
// - uia_find_files_by_pattern.json, uia_find_files_by_extension.json
// - uia_smart_file_organizer.json, uia_cleanup_downloads_folder.json
// - uia_bulk_rename_files.json
// - uia_find_element_by_text_browser.json, uia_extract_page_text_browser.json
//
// USAGE: Use EXECUTE_SCRIPT command to call nested scripts with parameters
//
// Example:
// {
//   "command": "EXECUTE_SCRIPT",
//   "parameters": {
//     "script_path": "test_scripts/uia_find_windows_by_title.json",
//     "pass_variables": {"titlePattern": "Notepad", "exactMatch": false}
//   }
// }
//
// =============================================================================

/*
REMOVED HARDCODED FUNCTIONS (replaced with atomic operations + scripts):

- FindApplicationWindows() -> Use UIA_FIND_WINDOWS_BY_CLASS + UIA_FIND_WINDOWS_BY_TITLE
- BringWindowToFront() -> Use uia_bring_window_to_front.json script  
- VerifyWindowFocus() -> Use UIA_GET_FOREGROUND_WINDOW atomic operation
- AggressiveWindowFocus() -> Use uia_aggressive_window_focus.json script
- SafeAutomationAbort() -> Use UIA_GET_FOREGROUND_WINDOW + conditional logic
- ClearDesktop() -> Use uia_clear_desktop.json script (Windows+D)
- CreateTestDirectory() -> Use UIA_CREATE_DIRECTORY atomic operation
- OpenFileExplorer() -> Use uia_open_file_explorer.json script
- FocusExplorer() -> Use UIA_FOCUS_WINDOW atomic operation
- LaunchProcess() -> Use uia_launch_process.json script
- FindWindowsByClassName() -> Use UIA_FIND_WINDOWS_BY_CLASS atomic operation
- BringWindowToForeground() -> Use UIA_FOCUS_WINDOW atomic operation
- FindFilesByPattern() -> Use uia_find_files_by_pattern.json script
- FindFilesByExtension() -> Use uia_find_files_by_extension.json script
- OrganizeFilesByDate() -> Use uia_smart_file_organizer.json script
- OrganizeFilesByType() -> Use uia_smart_file_organizer.json script
- CleanupDownloadsFolder() -> Use uia_cleanup_downloads_folder.json script
- BulkRenameFiles() -> Use uia_bulk_rename_files.json script
- MoveFilesToFolder() -> Use UIA_MOVE_FILES atomic operation
- DeleteOldFiles() -> Use UIA_DELETE_FILES atomic operation
- FindDuplicateFiles() -> Use UIA_FIND_FILES_BY_PATTERN + custom logic
- GetFileInfo() -> Use UIA_GET_FILE_INFO atomic operation
- LaunchApplication() -> Use UIA_LAUNCH_APPLICATION atomic operation
- TerminateProcessSafe() -> Use UIA_TERMINATE_PROCESS atomic operation
- FindWindowsByTitle() -> Use UIA_FIND_WINDOWS_BY_TITLE atomic operation
- FindWindowsByClass() -> Use UIA_FIND_WINDOWS_BY_CLASS atomic operation
- GetWindowInfo() -> Use UIA_GET_WINDOW_INFO atomic operation
- FocusWindowSafe() -> Use UIA_FOCUS_WINDOW atomic operation
- WaitForWindow() -> Use uia_wait_for_window.json script
- FindElementByText() -> Use uia_find_element_by_text_browser.json script
- ExtractPageText() -> Use uia_extract_page_text_browser.json script
- And many other hardcoded automation functions...

All functionality is now available through atomic operations and nested scripts.
*/

#endif // _WIN32

} // namespace universal

} // namespace application  
} // namespace ocal
