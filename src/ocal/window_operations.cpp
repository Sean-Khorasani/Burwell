#include "window_operations.h"
#include "../common/structured_logger.h"
#include "../common/input_validator.h"
#include <algorithm>
#include <vector>

namespace ocal {
namespace atomic {
namespace window {

// Helper function to convert UTF-16 to UTF-8
std::string wideStringToUtf8(const wchar_t* wideStr) {
    if (!wideStr) return "";
    
    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) return "";
    
    std::vector<char> utf8Buffer(utf8Length);
    WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, utf8Buffer.data(), utf8Length, nullptr, nullptr);
    return std::string(utf8Buffer.data());
}

#ifdef _WIN32

// Helper function for window enumeration callback
struct EnumWindowsData {
    std::map<uintptr_t, std::map<std::string, std::string>>* results;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    EnumWindowsData* data = reinterpret_cast<EnumWindowsData*>(lParam);
    
    if (!IsWindow(hwnd)) return TRUE;
    
    wchar_t windowTitle[256] = {0};
    wchar_t windowClass[256] = {0};
    RECT windowRect;
    
    GetWindowTextW(hwnd, windowTitle, sizeof(windowTitle) / sizeof(wchar_t));
    GetClassNameW(hwnd, windowClass, sizeof(windowClass) / sizeof(wchar_t));
    GetWindowRect(hwnd, &windowRect);
    
    // Get extended style
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    bool isToolWindow = (exStyle & WS_EX_TOOLWINDOW) != 0;
    
    std::map<std::string, std::string> windowInfo;
    windowInfo["title"] = wideStringToUtf8(windowTitle);
    windowInfo["className"] = wideStringToUtf8(windowClass);
    windowInfo["left"] = std::to_string(windowRect.left);
    windowInfo["top"] = std::to_string(windowRect.top);
    windowInfo["right"] = std::to_string(windowRect.right);
    windowInfo["bottom"] = std::to_string(windowRect.bottom);
    windowInfo["width"] = std::to_string(windowRect.right - windowRect.left);
    windowInfo["height"] = std::to_string(windowRect.bottom - windowRect.top);
    windowInfo["isVisible"] = IsWindowVisible(hwnd) ? "true" : "false";
    windowInfo["isMinimized"] = IsIconic(hwnd) ? "true" : "false";
    windowInfo["isMaximized"] = IsZoomed(hwnd) ? "true" : "false";
    windowInfo["isToolWindow"] = isToolWindow ? "true" : "false";
    
    (*data->results)[reinterpret_cast<uintptr_t>(hwnd)] = windowInfo;
    
    return TRUE;
}

bool enumerateWindows(std::map<uintptr_t, std::map<std::string, std::string>>& results) {
    EnumWindowsData data;
    data.results = &results;
    
    BOOL success = EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
    SLOG_DEBUG().message("[ATOMIC] Enumerated windows")
        .context("count", results.size());
    
    return success != FALSE;
}

bool findWindowsByClass(const std::string& className, std::vector<uintptr_t>& results) {
    // Validate className - should not be empty and should be reasonable length
    if (!burwell::InputValidator::isNotEmpty(className)) {
        SLOG_ERROR().message("[ATOMIC] Empty class name provided");
        return false;
    }
    
    if (className.length() > 256) {
        SLOG_ERROR().message("[ATOMIC] Class name too long")
            .context("length", className.length())
            .context("max_length", 256);
        return false;
    }
    
    std::wstring wClassName(className.begin(), className.end());
    
    struct SearchData {
        std::wstring targetClass;
        std::vector<uintptr_t>* results;
    } searchData = {wClassName, &results};
    
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        SearchData* data = reinterpret_cast<SearchData*>(lParam);
        
        wchar_t windowClass[256];
        if (GetClassNameW(hwnd, windowClass, sizeof(windowClass) / sizeof(wchar_t))) {
            std::wstring wWindowClass(windowClass);
            if (wWindowClass.find(data->targetClass) != std::wstring::npos) {
                data->results->push_back(reinterpret_cast<uintptr_t>(hwnd));
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&searchData));
    
    SLOG_DEBUG().message("[ATOMIC] Found windows with class")
        .context("count", results.size())
        .context("class", className);
    return true;
}

bool findWindowsByTitle(const std::string& titlePattern, bool exactMatch, std::vector<uintptr_t>& results) {
    // Validate title pattern
    if (!burwell::InputValidator::isNotEmpty(titlePattern)) {
        SLOG_ERROR().message("[ATOMIC] Empty title pattern provided");
        return false;
    }
    
    if (titlePattern.length() > 256) {
        SLOG_ERROR().message("[ATOMIC] Title pattern too long")
            .context("length", titlePattern.length())
            .context("max_length", 256);
        return false;
    }
    
    std::wstring wTitlePattern(titlePattern.begin(), titlePattern.end());
    
    struct SearchData {
        std::wstring pattern;
        bool exact;
        std::vector<uintptr_t>* results;
    } searchData = {wTitlePattern, exactMatch, &results};
    
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        SearchData* data = reinterpret_cast<SearchData*>(lParam);
        
        char windowTitle[256];
        if (GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle))) {
            std::wstring wWindowTitle(windowTitle, windowTitle + strlen(windowTitle));
            
            bool matches = false;
            if (data->exact) {
                matches = (wWindowTitle == data->pattern);
            } else {
                matches = (wWindowTitle.find(data->pattern) != std::wstring::npos);
            }
            
            if (matches) {
                data->results->push_back(reinterpret_cast<uintptr_t>(hwnd));
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&searchData));
    
    SLOG_DEBUG().message("[ATOMIC] Found windows with title pattern")
        .context("count", results.size())
        .context("pattern", titlePattern)
        .context("exact_match", exactMatch);
    return true;
}

bool focusWindow(uintptr_t hwnd) {
    // Validate handle is not null
    if (hwnd == 0) {
        SLOG_ERROR().message("[ATOMIC] Null window handle provided for focus");
        return false;
    }
    
    HWND windowHandle = reinterpret_cast<HWND>(hwnd);
    
    if (!IsWindow(windowHandle)) {
        SLOG_ERROR().message("[ATOMIC] Invalid window handle for focus")
            .context("handle", hwnd);
        return false;
    }
    
    // If window is minimized, restore it first
    if (IsIconic(windowHandle)) {
        ShowWindow(windowHandle, SW_RESTORE);
        SLOG_DEBUG().message("[ATOMIC] Restored minimized window");
    }
    
    // Try multiple approaches to bring window to front
    bool foregroundSuccess = SetForegroundWindow(windowHandle);
    bool activeSuccess = SetActiveWindow(windowHandle);
    bool showSuccess = ShowWindow(windowHandle, SW_SHOW);
    
    // Additional attempt with BringWindowToTop
    bool bringToTopSuccess = BringWindowToTop(windowHandle);
    
    // Consider it successful if window is valid and at least one method worked
    bool anySuccess = foregroundSuccess || activeSuccess || showSuccess || bringToTopSuccess;
    
    if (foregroundSuccess && activeSuccess) {
        SLOG_DEBUG().message("[ATOMIC] Window focused successfully (full focus)");
    } else if (anySuccess) {
        SLOG_DEBUG().message("[ATOMIC] Window brought to front (partial focus due to Windows security policy)");
    } else {
        SLOG_WARNING().message("[ATOMIC] All focus attempts failed");
    }
    
    return anySuccess;
}

bool getWindowTitle(uintptr_t hwnd, std::string& title) {
    HWND windowHandle = reinterpret_cast<HWND>(hwnd);
    
    if (!IsWindow(windowHandle)) {
        return false;
    }
    
    wchar_t windowTitle[256] = {0};
    int result = GetWindowTextW(windowHandle, windowTitle, sizeof(windowTitle) / sizeof(wchar_t));
    
    if (result > 0) {
        title = wideStringToUtf8(windowTitle);
        return true;
    }
    
    return false;
}

bool getWindowClass(uintptr_t hwnd, std::string& className) {
    HWND windowHandle = reinterpret_cast<HWND>(hwnd);
    
    if (!IsWindow(windowHandle)) {
        return false;
    }
    
    wchar_t windowClass[256] = {0};
    int result = GetClassNameW(windowHandle, windowClass, sizeof(windowClass) / sizeof(wchar_t));
    
    if (result > 0) {
        className = wideStringToUtf8(windowClass);
        return true;
    }
    
    return false;
}

bool getWindowRect(uintptr_t hwnd, std::map<std::string, int>& rect) {
    HWND windowHandle = reinterpret_cast<HWND>(hwnd);
    
    if (!IsWindow(windowHandle)) {
        return false;
    }
    
    RECT windowRect;
    if (GetWindowRect(windowHandle, &windowRect)) {
        rect["left"] = windowRect.left;
        rect["top"] = windowRect.top;
        rect["right"] = windowRect.right;
        rect["bottom"] = windowRect.bottom;
        rect["width"] = windowRect.right - windowRect.left;
        rect["height"] = windowRect.bottom - windowRect.top;
        return true;
    }
    
    return false;
}

bool getWindowInfo(uintptr_t hwnd, std::map<std::string, std::string>& info) {
    HWND windowHandle = reinterpret_cast<HWND>(hwnd);
    
    if (!IsWindow(windowHandle)) {
        return false;
    }
    
    wchar_t windowTitle[256] = {0};
    wchar_t windowClass[256] = {0};
    RECT windowRect;
    
    GetWindowTextW(windowHandle, windowTitle, sizeof(windowTitle) / sizeof(wchar_t));
    GetClassNameW(windowHandle, windowClass, sizeof(windowClass) / sizeof(wchar_t));
    GetWindowRect(windowHandle, &windowRect);
    
    info["title"] = wideStringToUtf8(windowTitle);
    info["className"] = wideStringToUtf8(windowClass);
    info["left"] = std::to_string(windowRect.left);
    info["top"] = std::to_string(windowRect.top);
    info["right"] = std::to_string(windowRect.right);
    info["bottom"] = std::to_string(windowRect.bottom);
    info["width"] = std::to_string(windowRect.right - windowRect.left);
    info["height"] = std::to_string(windowRect.bottom - windowRect.top);
    info["isVisible"] = IsWindowVisible(windowHandle) ? "true" : "false";
    info["isMinimized"] = IsIconic(windowHandle) ? "true" : "false";
    info["isMaximized"] = IsZoomed(windowHandle) ? "true" : "false";
    
    return true;
}

bool isWindowMinimized(uintptr_t hwnd, bool& isMinimized) {
    HWND windowHandle = reinterpret_cast<HWND>(hwnd);
    
    if (!IsWindow(windowHandle)) {
        return false;
    }
    
    isMinimized = IsIconic(windowHandle) != 0;
    return true;
}

bool isWindowMaximized(uintptr_t hwnd, bool& isMaximized) {
    HWND windowHandle = reinterpret_cast<HWND>(hwnd);
    
    if (!IsWindow(windowHandle)) {
        return false;
    }
    
    isMaximized = IsZoomed(windowHandle) != 0;
    return true;
}

bool getForegroundWindow(uintptr_t& hwnd) {
    HWND foregroundWindow = GetForegroundWindow();
    
    if (foregroundWindow) {
        hwnd = reinterpret_cast<uintptr_t>(foregroundWindow);
        return true;
    }
    
    return false;
}

bool resizeWindow(uintptr_t hwnd, int width, int height) {
    // Validate parameters
    if (hwnd == 0) {
        SLOG_ERROR().message("[ATOMIC] Null window handle provided for resize");
        return false;
    }
    
    if (width <= 0 || height <= 0) {
        SLOG_ERROR().message("[ATOMIC] Invalid window dimensions")
            .context("width", width)
            .context("height", height);
        return false;
    }
    
    // Reasonable size limits
    const int MAX_DIMENSION = 10000;
    if (width > MAX_DIMENSION || height > MAX_DIMENSION) {
        SLOG_ERROR().message("[ATOMIC] Window dimensions exceed maximum")
            .context("width", width)
            .context("height", height)
            .context("max", MAX_DIMENSION);
        return false;
    }
    
    HWND windowHandle = reinterpret_cast<HWND>(hwnd);
    
    if (!IsWindow(windowHandle)) {
        SLOG_ERROR().message("[ATOMIC] Invalid window handle for resize")
            .context("handle", hwnd);
        return false;
    }
    
    return SetWindowPos(windowHandle, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER) != FALSE;
}

bool moveWindow(uintptr_t hwnd, int x, int y) {
    // Validate parameters
    if (hwnd == 0) {
        SLOG_ERROR().message("[ATOMIC] Null window handle provided for move");
        return false;
    }
    
    // Validate coordinates are within reasonable screen bounds
    const int MIN_COORD = -10000;
    const int MAX_COORD = 10000;
    if (x < MIN_COORD || x > MAX_COORD || y < MIN_COORD || y > MAX_COORD) {
        SLOG_ERROR().message("[ATOMIC] Window coordinates out of reasonable bounds")
            .context("x", x)
            .context("y", y)
            .context("min", MIN_COORD)
            .context("max", MAX_COORD);
        return false;
    }
    
    HWND windowHandle = reinterpret_cast<HWND>(hwnd);
    
    if (!IsWindow(windowHandle)) {
        SLOG_ERROR().message("[ATOMIC] Invalid window handle for move")
            .context("handle", hwnd);
        return false;
    }
    
    return SetWindowPos(windowHandle, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER) != FALSE;
}

#endif // _WIN32

} // namespace window
} // namespace atomic
} // namespace ocal