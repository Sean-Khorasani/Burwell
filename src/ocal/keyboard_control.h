#ifndef BURWELL_KEYBOARD_CONTROL_H
#define BURWELL_KEYBOARD_CONTROL_H

#include <string>
#include <vector>

namespace ocal {
namespace keyboard {

enum class Key {
    // Letters
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    
    // Numbers
    NUM_0, NUM_1, NUM_2, NUM_3, NUM_4, NUM_5, NUM_6, NUM_7, NUM_8, NUM_9,
    
    // Function keys
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    
    // Special keys
    SPACE, ENTER, TAB, BACKSPACE, DELETE_KEY, ESCAPE,
    SHIFT_LEFT, SHIFT_RIGHT, CTRL_LEFT, CTRL_RIGHT, ALT_LEFT, ALT_RIGHT,
    WINDOWS_LEFT, WINDOWS_RIGHT, MENU,
    
    // Arrow keys
    ARROW_UP, ARROW_DOWN, ARROW_LEFT, ARROW_RIGHT,
    
    // Home/End keys
    HOME, END, PAGE_UP, PAGE_DOWN, INSERT,
    
    // Lock keys
    CAPS_LOCK, NUM_LOCK, SCROLL_LOCK,
    
    // Numpad
    NUMPAD_0, NUMPAD_1, NUMPAD_2, NUMPAD_3, NUMPAD_4,
    NUMPAD_5, NUMPAD_6, NUMPAD_7, NUMPAD_8, NUMPAD_9,
    NUMPAD_MULTIPLY, NUMPAD_ADD, NUMPAD_SUBTRACT, NUMPAD_DECIMAL, NUMPAD_DIVIDE,
    
    // Punctuation and symbols
    SEMICOLON, EQUALS, COMMA, MINUS, PERIOD, SLASH, BACKTICK,
    LEFT_BRACKET, BACKSLASH, RIGHT_BRACKET, QUOTE
};

using KeyCombination = std::vector<Key>;

// Core keyboard functions
void press(Key key);
void release(Key key);
void tap(Key key); // Press and release quickly
void type(const std::string& text);

// Hotkey/combination functions
void hotkey(const KeyCombination& combination);
void hotkey(std::initializer_list<Key> keys);
void pressHotkey(const KeyCombination& combination); // Hold down combination
void releaseHotkey(const KeyCombination& combination); // Release combination

// Advanced keyboard functions
void sendText(const std::string& text, int delayMs = 0); // Type with delay between chars
void sendKeyCode(int virtualKeyCode, bool extended = false);
void sendScanCode(int scanCode, bool extended = false);

// State functions
bool isKeyPressed(Key key);
bool isCapsLockOn();
bool isNumLockOn();
bool isScrollLockOn();

// Utility functions
void logCurrentKeyboardSettings(); // Log current CPL configuration settings
Key charToKey(char c);
std::string keyToString(Key key);

// Common hotkey shortcuts
namespace shortcuts {
    void copy();        // Ctrl+C
    void paste();       // Ctrl+V
    void cut();         // Ctrl+X
    void undo();        // Ctrl+Z
    void redo();        // Ctrl+Y
    void selectAll();   // Ctrl+A
    void save();        // Ctrl+S
    void find();        // Ctrl+F
    void newWindow();   // Ctrl+N
    void closeWindow(); // Alt+F4
    void switchApp();   // Alt+Tab
    void showDesktop(); // Windows+D
}

} // namespace keyboard
} // namespace ocal

#endif // BURWELL_KEYBOARD_CONTROL_H