#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <string>
#include <vector>
#include <map>

namespace ocal {
namespace atomic {
namespace window {

#ifdef _WIN32

/**
 * Enumerate all windows and return window information
 * @param results Map to store window handle -> info mapping
 * @return true if enumeration successful
 */
bool enumerateWindows(std::map<uintptr_t, std::map<std::string, std::string>>& results);

/**
 * Find windows by class name pattern
 * @param className Class name pattern to search for
 * @param results Vector to store matching window handles
 * @return true if search completed successfully
 */
bool findWindowsByClass(const std::string& className, std::vector<uintptr_t>& results);

/**
 * Find windows by title pattern
 * @param titlePattern Title pattern to search for
 * @param exactMatch Whether to require exact match
 * @param results Vector to store matching window handles
 * @return true if search completed successfully
 */
bool findWindowsByTitle(const std::string& titlePattern, bool exactMatch, std::vector<uintptr_t>& results);

/**
 * Focus a window using its handle
 * @param hwnd Window handle to focus
 * @return true if window focused successfully
 */
bool focusWindow(uintptr_t hwnd);

/**
 * Get window title
 * @param hwnd Window handle
 * @param title Output string for window title
 * @return true if title retrieved successfully
 */
bool getWindowTitle(uintptr_t hwnd, std::string& title);

/**
 * Get window class name
 * @param hwnd Window handle
 * @param className Output string for class name
 * @return true if class name retrieved successfully
 */
bool getWindowClass(uintptr_t hwnd, std::string& className);

/**
 * Get window rectangle (position and size)
 * @param hwnd Window handle
 * @param rect Output map containing left, top, right, bottom, width, height
 * @return true if rectangle retrieved successfully
 */
bool getWindowRect(uintptr_t hwnd, std::map<std::string, int>& rect);

/**
 * Get comprehensive window information
 * @param hwnd Window handle
 * @param info Output map containing all window properties
 * @return true if information retrieved successfully
 */
bool getWindowInfo(uintptr_t hwnd, std::map<std::string, std::string>& info);

/**
 * Check if window is minimized
 * @param hwnd Window handle
 * @param isMinimized Output boolean
 * @return true if check completed successfully
 */
bool isWindowMinimized(uintptr_t hwnd, bool& isMinimized);

/**
 * Check if window is maximized
 * @param hwnd Window handle
 * @param isMaximized Output boolean
 * @return true if check completed successfully
 */
bool isWindowMaximized(uintptr_t hwnd, bool& isMaximized);

/**
 * Get foreground (currently focused) window
 * @param hwnd Output window handle
 * @return true if foreground window retrieved successfully
 */
bool getForegroundWindow(uintptr_t& hwnd);

/**
 * Resize window to specified dimensions
 * @param hwnd Window handle
 * @param width New width
 * @param height New height
 * @return true if resize successful
 */
bool resizeWindow(uintptr_t hwnd, int width, int height);

/**
 * Move window to specified position
 * @param hwnd Window handle
 * @param x New X coordinate
 * @param y New Y coordinate
 * @return true if move successful
 */
bool moveWindow(uintptr_t hwnd, int x, int y);

#endif // _WIN32

} // namespace window
} // namespace atomic
} // namespace ocal