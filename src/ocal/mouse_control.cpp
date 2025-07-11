#include "mouse_control.h"
#include "../common/structured_logger.h"
#include "../common/error_handler.h"
#include "../common/os_utils.h"
#include "../cpl/cpl_config_loader.h"
#include <thread>
#include <chrono>
#include <cmath>

namespace ocal {
namespace mouse {

namespace {
    // Get settings from CPL configuration instead of hardcoded values
    int getMouseSpeed() {
        return burwell::cpl::CPLConfigLoader::getInstance().getMouseSpeed();
    }
    
    bool getSmoothMovement() {
        return burwell::cpl::CPLConfigLoader::getInstance().getMouseSmoothMovement();
    }
    
    int getClickDelay() {
        return burwell::cpl::CPLConfigLoader::getInstance().getMouseClickDelay();
    }
    
    int getDoubleClickInterval() {
        return burwell::cpl::CPLConfigLoader::getInstance().getMouseDoubleClickInterval();
    }
    
    // Platform-specific function implementations
    void moveMouseToPlatformSpecific(int x, int y);
    void clickMousePlatformSpecific(MouseButton button, ClickType type);
    Point getPositionPlatformSpecific();
    bool isButtonPressedPlatformSpecific(MouseButton button);
    void scrollPlatformSpecific(int deltaX, int deltaY);
    void pressPlatformSpecific(MouseButton button);
    void releasePlatformSpecific(MouseButton button);
    
#ifdef _WIN32
    DWORD getMouseButtonFlag(MouseButton button, bool isDown) {
        switch (button) {
            case MouseButton::LEFT:
                return isDown ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            case MouseButton::RIGHT:
                return isDown ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            case MouseButton::MIDDLE:
                return isDown ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            default:
                return MOUSEEVENTF_LEFTDOWN;
        }
    }
    
    void smoothMoveTo(int targetX, int targetY) {
        bool smoothMovement = getSmoothMovement();
        if (!smoothMovement) {
            SetCursorPos(targetX, targetY);
            return;
        }
        
        POINT currentPos;
        GetCursorPos(&currentPos);
        
        int deltaX = targetX - currentPos.x;
        int deltaY = targetY - currentPos.y;
        int distance = static_cast<int>(std::sqrt(deltaX * deltaX + deltaY * deltaY));
        
        if (distance == 0) return;
        
        int mouseSpeed = getMouseSpeed(); // 1-100
        
        // For automation speed: High speed = fewer steps and minimal delay
        // Speed 100 = instant (1 step), Speed 1 = very smooth (many steps)
        int maxSteps = (mouseSpeed >= 90) ? 1 :          // Speed 90-100: Instant
                       (mouseSpeed >= 70) ? 3 :          // Speed 70-89: Very fast (3 steps)
                       (mouseSpeed >= 50) ? 8 :          // Speed 50-69: Fast (8 steps) 
                       (mouseSpeed >= 30) ? 20 :         // Speed 30-49: Medium (20 steps)
                       50;                               // Speed 1-29: Smooth (50 steps)
        
        int steps = std::min(maxSteps, distance);
        
        // Delay calculation: High speed = minimal delay
        int delayMs = (mouseSpeed >= 90) ? 0 :           // Speed 90-100: No delay
                      (mouseSpeed >= 70) ? 1 :           // Speed 70-89: 1ms delay
                      (mouseSpeed >= 50) ? 2 :           // Speed 50-69: 2ms delay
                      (mouseSpeed >= 30) ? 5 :           // Speed 30-49: 5ms delay
                      10;                                // Speed 1-29: 10ms delay
        
        if (steps <= 1) {
            // For high speed or short distance, just move directly
            SetCursorPos(targetX, targetY);
        } else {
            // Smooth movement with calculated steps and delay
            for (int i = 1; i <= steps; ++i) {
                int x = currentPos.x + (deltaX * i) / steps;
                int y = currentPos.y + (deltaY * i) / steps;
                SetCursorPos(x, y);
                if (delayMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                }
            }
            
            // Ensure we reach exact target
            SetCursorPos(targetX, targetY);
        }
    }
#endif
    
    // Platform-specific function implementations
    void moveMouseToPlatformSpecific(int x, int y) {
    #ifdef _WIN32
        smoothMoveTo(x, y);
    #else
        (void)x; (void)y;
        SLOG_DEBUG().message("Mouse move simulated (non-Windows platform)");
    #endif
    }
    
    void clickMousePlatformSpecific(MouseButton button, ClickType type) {
    #ifdef _WIN32
        DWORD downFlag = getMouseButtonFlag(button, true);
        DWORD upFlag = getMouseButtonFlag(button, false);
        
        int clickDelay = getClickDelay();
        int doubleClickInterval = getDoubleClickInterval();
        
        mouse_event(downFlag, 0, 0, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(clickDelay));
        mouse_event(upFlag, 0, 0, 0, 0);
        
        if (type == ClickType::DOUBLE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(doubleClickInterval));
            mouse_event(downFlag, 0, 0, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(clickDelay));
            mouse_event(upFlag, 0, 0, 0, 0);
        }
    #else
        (void)button; (void)type;
        SLOG_DEBUG().message("Mouse click simulated (non-Windows platform)");
    #endif
    }
    
    Point getPositionPlatformSpecific() {
        Point position;
    #ifdef _WIN32
        POINT pt;
        if (GetCursorPos(&pt)) {
            position.x = pt.x;
            position.y = pt.y;
        } else {
            BURWELL_HANDLE_ERROR(ErrorType::OS_OPERATION_ERROR, ErrorSeverity::MEDIUM,
                                  "Failed to get mouse position", "", "mouse::getPosition");
        }
    #else
        position.x = 0;
        position.y = 0;
    #endif
        return position;
    }
    
    bool isButtonPressedPlatformSpecific(MouseButton button) {
    #ifdef _WIN32
        int vKey;
        switch (button) {
            case MouseButton::LEFT: vKey = VK_LBUTTON; break;
            case MouseButton::RIGHT: vKey = VK_RBUTTON; break;
            case MouseButton::MIDDLE: vKey = VK_MBUTTON; break;
            default: vKey = VK_LBUTTON; break;
        }
        return (GetAsyncKeyState(vKey) & 0x8000) != 0;
    #else
        (void)button;
        return false;
    #endif
    }
    
    void scrollPlatformSpecific(int deltaX, int deltaY) {
    #ifdef _WIN32
        if (deltaY != 0) {
            mouse_event(MOUSEEVENTF_WHEEL, 0, 0, deltaY * WHEEL_DELTA, 0);
        }
        if (deltaX != 0) {
            mouse_event(MOUSEEVENTF_HWHEEL, 0, 0, deltaX * WHEEL_DELTA, 0);
        }
    #else
        (void)deltaX; (void)deltaY;
        SLOG_DEBUG().message("Mouse scroll simulated (non-Windows platform)");
    #endif
    }
    
    void pressPlatformSpecific(MouseButton button) {
    #ifdef _WIN32
        DWORD downFlag = getMouseButtonFlag(button, true);
        mouse_event(downFlag, 0, 0, 0, 0);
    #else
        (void)button;
        SLOG_DEBUG().message("Mouse press simulated (non-Windows platform)");
    #endif
    }
    
    void releasePlatformSpecific(MouseButton button) {
    #ifdef _WIN32
        DWORD upFlag = getMouseButtonFlag(button, false);
        mouse_event(upFlag, 0, 0, 0, 0);
    #else
        (void)button;
        SLOG_DEBUG().message("Mouse release simulated (non-Windows platform)");
    #endif
    }
}

void move(int x, int y) {
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Moving mouse to position").context("x", x).context("y", y);
        
        // Platform-specific mouse movement
        moveMouseToPlatformSpecific(x, y);
    }, "mouse::move");
}

void moveMouseToPlatformSpecific(int x, int y) {
#ifdef _WIN32
    smoothMoveTo(x, y);
#else
    // For non-Windows platforms (development/testing)
    (void)x;
    (void)y;
    SLOG_DEBUG().message("Mouse move simulated (non-Windows platform)");
#endif
}

void move(const Point& point) {
    move(point.x, point.y);
}

void click(MouseButton button, ClickType type) {
    BURWELL_TRY_CATCH({
        std::string buttonStr = (button == MouseButton::LEFT) ? "LEFT" : 
                               (button == MouseButton::RIGHT) ? "RIGHT" : "MIDDLE";
        std::string typeStr = (type == ClickType::SINGLE) ? "SINGLE" : "DOUBLE";
        SLOG_DEBUG().message("Mouse click").context("type", typeStr).context("button", buttonStr);
        
        // Platform-specific mouse click
        clickMousePlatformSpecific(button, type);
    }, "mouse::click");
}

void clickMousePlatformSpecific(MouseButton button, ClickType type) {
#ifdef _WIN32
    DWORD downFlag = getMouseButtonFlag(button, true);
    DWORD upFlag = getMouseButtonFlag(button, false);
    
    int clickDelay = getClickDelay();
    int doubleClickInterval = getDoubleClickInterval();
    
    mouse_event(downFlag, 0, 0, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(clickDelay));
    mouse_event(upFlag, 0, 0, 0, 0);
    
    if (type == ClickType::DOUBLE) {
        std::this_thread::sleep_for(std::chrono::milliseconds(doubleClickInterval));
        mouse_event(downFlag, 0, 0, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(clickDelay));
        mouse_event(upFlag, 0, 0, 0, 0);
    }
#else
    (void)button;
    (void)type;
    SLOG_DEBUG().message("Mouse click simulated (non-Windows platform)");
#endif
}

void clickAt(int x, int y, MouseButton button, ClickType type) {
    move(x, y);
    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Small delay after move
    click(button, type);
}

void clickAt(const Point& point, MouseButton button, ClickType type) {
    clickAt(point.x, point.y, button, type);
}

void drag(const Point& start, const Point& end, MouseButton button) {
    drag(start.x, start.y, end.x, end.y, button);
}

void drag(int startX, int startY, int endX, int endY, MouseButton button) {
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Mouse drag").context("startX", startX).context("startY", startY).context("endX", endX).context("endY", endY);
        
        move(startX, startY);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        press(button);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        move(endX, endY);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        release(button);
    }, "mouse::drag");
}

Point getPosition() {
    Point position;
    
    BURWELL_TRY_CATCH({
        position = getPositionPlatformSpecific();
    }, "mouse::getPosition");
    
    return position;
}

bool isButtonPressed(MouseButton button) {
    BURWELL_TRY_CATCH({
        return isButtonPressedPlatformSpecific(button);
    }, "mouse::isButtonPressed");
    
    return false;
}

void scroll(int deltaX, int deltaY) {
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Mouse scroll").context("deltaX", deltaX).context("deltaY", deltaY);
        
        // Platform-specific mouse scroll
        scrollPlatformSpecific(deltaX, deltaY);
    }, "mouse::scroll");
}

void press(MouseButton button) {
    BURWELL_TRY_CATCH({
        std::string buttonStr = (button == MouseButton::LEFT) ? "LEFT" : 
                               (button == MouseButton::RIGHT) ? "RIGHT" : "MIDDLE";
        SLOG_DEBUG().message("Mouse press").context("button", buttonStr);
        
        // Platform-specific mouse press
        pressPlatformSpecific(button);
    }, "mouse::press");
}

void release(MouseButton button) {
    BURWELL_TRY_CATCH({
        std::string buttonStr = (button == MouseButton::LEFT) ? "LEFT" : 
                               (button == MouseButton::RIGHT) ? "RIGHT" : "MIDDLE";
        SLOG_DEBUG().message("Mouse release").context("button", buttonStr);
        
        // Platform-specific mouse release
        releasePlatformSpecific(button);
    }, "mouse::release");
}

void ensurePosition(const Point& position) {
    Point current = getPosition();
    if (current.x != position.x || current.y != position.y) {
        move(position);
    }
}

void logCurrentSettings() {
    int speed = getMouseSpeed();
    bool smooth = getSmoothMovement();
    int clickDelay = getClickDelay();
    int doubleClickInterval = getDoubleClickInterval();
    
    SLOG_DEBUG().message("Mouse settings from CPL config")
        .context("speed", speed)
        .context("smooth", smooth)
        .context("click_delay", clickDelay)
        .context("double_click_interval", doubleClickInterval);
}

} // namespace mouse
} // namespace ocal