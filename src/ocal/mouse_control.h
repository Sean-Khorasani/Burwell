#ifndef BURWELL_MOUSE_CONTROL_H
#define BURWELL_MOUSE_CONTROL_H

#include <utility>

namespace ocal {
namespace mouse {

enum class MouseButton {
    LEFT,
    RIGHT,
    MIDDLE
};

enum class ClickType {
    SINGLE,
    DOUBLE
};

struct Point {
    int x;
    int y;
    
    Point(int x = 0, int y = 0) : x(x), y(y) {}
};

// Core mouse control functions
void move(int x, int y);
void move(const Point& point);
void click(MouseButton button = MouseButton::LEFT, ClickType type = ClickType::SINGLE);
void clickAt(int x, int y, MouseButton button = MouseButton::LEFT, ClickType type = ClickType::SINGLE);
void clickAt(const Point& point, MouseButton button = MouseButton::LEFT, ClickType type = ClickType::SINGLE);
void drag(const Point& start, const Point& end, MouseButton button = MouseButton::LEFT);
void drag(int startX, int startY, int endX, int endY, MouseButton button = MouseButton::LEFT);

// Mouse state functions
Point getPosition();
bool isButtonPressed(MouseButton button);

// Advanced mouse functions
void scroll(int deltaX = 0, int deltaY = 0);
void press(MouseButton button);
void release(MouseButton button);

// Utility functions
void ensurePosition(const Point& position);
void logCurrentSettings(); // Log current CPL configuration settings

} // namespace mouse
} // namespace ocal

#endif // BURWELL_MOUSE_CONTROL_H