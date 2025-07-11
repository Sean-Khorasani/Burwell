#ifndef BURWELL_WINDOW_MANAGEMENT_H
#define BURWELL_WINDOW_MANAGEMENT_H

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace ocal {
namespace window {

#ifdef _WIN32
using WindowHandle = void*; // HWND on Windows
constexpr WindowHandle INVALID_WINDOW_HANDLE = nullptr;
#else
using WindowHandle = int;   // For non-Windows development  
constexpr WindowHandle INVALID_WINDOW_HANDLE = 0;
#endif

struct WindowInfo {
    WindowHandle handle;
    std::string title;
    std::string className;
    int x, y;           // Position
    int width, height;  // Size
    bool isVisible;
    bool isMinimized;
    bool isMaximized;
    unsigned long processId;
    
    WindowInfo() : handle(INVALID_WINDOW_HANDLE), x(0), y(0), width(0), height(0), 
                   isVisible(false), isMinimized(false), isMaximized(false), processId(0) {}
};

struct Rectangle {
    int left, top, right, bottom;
    
    Rectangle(int l = 0, int t = 0, int r = 0, int b = 0) 
        : left(l), top(t), right(r), bottom(b) {}
    
    int width() const { return right - left; }
    int height() const { return bottom - top; }
};

// Core window management functions
WindowHandle find(const std::string& title);
WindowHandle findByClassName(const std::string& className);
WindowHandle findByProcess(unsigned long processId);
std::vector<WindowHandle> findAll(const std::string& title);
WindowHandle getActive();
WindowHandle getForeground();

// Window information
WindowInfo getInfo(WindowHandle handle);
std::string getTitle(WindowHandle handle);
std::string getClassName(WindowHandle handle);
Rectangle getBounds(WindowHandle handle);
unsigned long getProcessId(WindowHandle handle);

// Window state operations
bool bringToFront(WindowHandle handle);
bool activate(WindowHandle handle);
bool minimize(WindowHandle handle);
bool maximize(WindowHandle handle);
bool restore(WindowHandle handle);
bool close(WindowHandle handle);
bool hide(WindowHandle handle);
bool show(WindowHandle handle);

// Window positioning and sizing
bool move(WindowHandle handle, int x, int y);
bool resize(WindowHandle handle, int width, int height);
bool setBounds(WindowHandle handle, const Rectangle& bounds);
bool center(WindowHandle handle);

// Window enumeration
std::vector<WindowInfo> enumerateAll();
std::vector<WindowInfo> enumerateVisible();
std::vector<WindowInfo> enumerateByProcess(unsigned long processId);

// Window hierarchy
WindowHandle getParent(WindowHandle handle);
std::vector<WindowHandle> getChildren(WindowHandle handle);
WindowHandle getRoot(WindowHandle handle);

// Window properties
bool isValid(WindowHandle handle);
bool isVisible(WindowHandle handle);
bool isMinimized(WindowHandle handle);
bool isMaximized(WindowHandle handle);
bool isEnabled(WindowHandle handle);
bool isTopmost(WindowHandle handle);

// Advanced window operations
bool setTitle(WindowHandle handle, const std::string& title);
bool setTopmost(WindowHandle handle, bool topmost);
bool setTransparency(WindowHandle handle, int alpha); // 0-255
bool flash(WindowHandle handle, int count = 1);

// Window monitoring
using WindowCallback = std::function<void(WindowHandle, const std::string& event)>;
bool startMonitoring(WindowCallback callback);
void stopMonitoring();

// Utility functions
bool waitForWindow(const std::string& title, int timeoutMs = 5000);
bool waitForWindowClose(WindowHandle handle, int timeoutMs = 5000);
WindowHandle findMainWindow(unsigned long processId);

// Screen and desktop functions
Rectangle getDesktopBounds();
int getScreenCount();
Rectangle getScreenBounds(int screenIndex = 0);

} // namespace window
} // namespace ocal

#endif // BURWELL_WINDOW_MANAGEMENT_H