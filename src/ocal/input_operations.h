#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <string>
#include <map>

namespace ocal {
namespace atomic {
namespace input {

#ifdef _WIN32

/**
 * Press a key (virtual key code)
 * @param key Virtual key code to press
 * @return true if key press successful
 */
bool pressKey(unsigned char key);

/**
 * Release a key (virtual key code)
 * @param key Virtual key code to release
 * @return true if key release successful
 */
bool releaseKey(unsigned char key);

/**
 * Click mouse button at current position
 * @param button Button to click ("left", "right", "middle")
 * @return true if click successful
 */
bool mouseClick(const std::string& button);

/**
 * Move mouse to specified coordinates
 * @param x X coordinate
 * @param y Y coordinate
 * @return true if move successful
 */
bool mouseMove(int x, int y);

/**
 * Get current mouse position
 * @param position Output map containing x and y coordinates
 * @return true if position retrieved successfully
 */
bool getMousePosition(std::map<std::string, int>& position);

/**
 * Get clipboard text content
 * @param text Output string for clipboard content
 * @return true if clipboard content retrieved successfully
 */
bool getClipboard(std::string& text);

/**
 * Set clipboard text content
 * @param text Text to set in clipboard
 * @return true if clipboard set successfully
 */
bool setClipboard(const std::string& text);

#endif // _WIN32

} // namespace input
} // namespace atomic
} // namespace ocal