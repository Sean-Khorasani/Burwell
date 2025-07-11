#include "keyboard_control.h"
#include "../common/structured_logger.h"
#include "../common/error_handler.h"
#include "../common/os_utils.h"
#include "../cpl/cpl_config_loader.h"
#include <thread>
#include <chrono>
#include <map>
#include <cctype>

namespace ocal {
namespace keyboard {

namespace {
    // Get settings from CPL configuration instead of hardcoded values
    int getTypingSpeed() {
        return burwell::cpl::CPLConfigLoader::getInstance().getKeyboardTypingSpeed();
    }
    
    int getKeyPressDuration() {
        return burwell::cpl::CPLConfigLoader::getInstance().getKeyboardKeyPressDuration();
    }
    
    int getKeyReleaseDelay() {
        return burwell::cpl::CPLConfigLoader::getInstance().getKeyboardKeyReleaseDelay();
    }
    
    int getModifierHoldTime() {
        return burwell::cpl::CPLConfigLoader::getInstance().getKeyboardModifierHoldTime();
    }
    
#ifdef _WIN32
    std::map<Key, int> keyToVirtualKey = {
        // Letters
        {Key::A, 'A'}, {Key::B, 'B'}, {Key::C, 'C'}, {Key::D, 'D'}, {Key::E, 'E'},
        {Key::F, 'F'}, {Key::G, 'G'}, {Key::H, 'H'}, {Key::I, 'I'}, {Key::J, 'J'},
        {Key::K, 'K'}, {Key::L, 'L'}, {Key::M, 'M'}, {Key::N, 'N'}, {Key::O, 'O'},
        {Key::P, 'P'}, {Key::Q, 'Q'}, {Key::R, 'R'}, {Key::S, 'S'}, {Key::T, 'T'},
        {Key::U, 'U'}, {Key::V, 'V'}, {Key::W, 'W'}, {Key::X, 'X'}, {Key::Y, 'Y'}, {Key::Z, 'Z'},
        
        // Numbers
        {Key::NUM_0, '0'}, {Key::NUM_1, '1'}, {Key::NUM_2, '2'}, {Key::NUM_3, '3'}, {Key::NUM_4, '4'},
        {Key::NUM_5, '5'}, {Key::NUM_6, '6'}, {Key::NUM_7, '7'}, {Key::NUM_8, '8'}, {Key::NUM_9, '9'},
        
        // Function keys
        {Key::F1, VK_F1}, {Key::F2, VK_F2}, {Key::F3, VK_F3}, {Key::F4, VK_F4},
        {Key::F5, VK_F5}, {Key::F6, VK_F6}, {Key::F7, VK_F7}, {Key::F8, VK_F8},
        {Key::F9, VK_F9}, {Key::F10, VK_F10}, {Key::F11, VK_F11}, {Key::F12, VK_F12},
        
        // Special keys
        {Key::SPACE, VK_SPACE}, {Key::ENTER, VK_RETURN}, {Key::TAB, VK_TAB},
        {Key::BACKSPACE, VK_BACK}, {Key::DELETE_KEY, VK_DELETE}, {Key::ESCAPE, VK_ESCAPE},
        {Key::SHIFT_LEFT, VK_LSHIFT}, {Key::SHIFT_RIGHT, VK_RSHIFT},
        {Key::CTRL_LEFT, VK_LCONTROL}, {Key::CTRL_RIGHT, VK_RCONTROL},
        {Key::ALT_LEFT, VK_LMENU}, {Key::ALT_RIGHT, VK_RMENU},
        {Key::WINDOWS_LEFT, VK_LWIN}, {Key::WINDOWS_RIGHT, VK_RWIN}, {Key::MENU, VK_APPS},
        
        // Arrow keys
        {Key::ARROW_UP, VK_UP}, {Key::ARROW_DOWN, VK_DOWN},
        {Key::ARROW_LEFT, VK_LEFT}, {Key::ARROW_RIGHT, VK_RIGHT},
        
        // Home/End keys
        {Key::HOME, VK_HOME}, {Key::END, VK_END}, {Key::PAGE_UP, VK_PRIOR},
        {Key::PAGE_DOWN, VK_NEXT}, {Key::INSERT, VK_INSERT},
        
        // Lock keys
        {Key::CAPS_LOCK, VK_CAPITAL}, {Key::NUM_LOCK, VK_NUMLOCK}, {Key::SCROLL_LOCK, VK_SCROLL},
        
        // Numpad
        {Key::NUMPAD_0, VK_NUMPAD0}, {Key::NUMPAD_1, VK_NUMPAD1}, {Key::NUMPAD_2, VK_NUMPAD2},
        {Key::NUMPAD_3, VK_NUMPAD3}, {Key::NUMPAD_4, VK_NUMPAD4}, {Key::NUMPAD_5, VK_NUMPAD5},
        {Key::NUMPAD_6, VK_NUMPAD6}, {Key::NUMPAD_7, VK_NUMPAD7}, {Key::NUMPAD_8, VK_NUMPAD8},
        {Key::NUMPAD_9, VK_NUMPAD9}, {Key::NUMPAD_MULTIPLY, VK_MULTIPLY}, {Key::NUMPAD_ADD, VK_ADD},
        {Key::NUMPAD_SUBTRACT, VK_SUBTRACT}, {Key::NUMPAD_DECIMAL, VK_DECIMAL}, {Key::NUMPAD_DIVIDE, VK_DIVIDE},
        
        // Punctuation
        {Key::SEMICOLON, VK_OEM_1}, {Key::EQUALS, VK_OEM_PLUS}, {Key::COMMA, VK_OEM_COMMA},
        {Key::MINUS, VK_OEM_MINUS}, {Key::PERIOD, VK_OEM_PERIOD}, {Key::SLASH, VK_OEM_2},
        {Key::BACKTICK, VK_OEM_3}, {Key::LEFT_BRACKET, VK_OEM_4}, {Key::BACKSLASH, VK_OEM_5},
        {Key::RIGHT_BRACKET, VK_OEM_6}, {Key::QUOTE, VK_OEM_7}
    };
    
    void sendKeyEvent(int virtualKey, bool isDown, bool extended = false) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = virtualKey;
        input.ki.dwFlags = isDown ? 0 : KEYEVENTF_KEYUP;
        if (extended) {
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        
        SendInput(1, &input, sizeof(INPUT));
    }
    
    [[maybe_unused]] int getTypingDelayMs() {
        // Convert characters per second to milliseconds per character
        int typingSpeed = getTypingSpeed(); // chars per second
        return (typingSpeed > 0) ? (1000 / typingSpeed) : 50; // Default to 50ms if invalid
    }
    
    bool needsShift(char c) {
        return std::isupper(c) || 
               c == '!' || c == '@' || c == '#' || c == '$' || c == '%' ||
               c == '^' || c == '&' || c == '*' || c == '(' || c == ')' ||
               c == '_' || c == '+' || c == '{' || c == '}' || c == '|' ||
               c == ':' || c == '"' || c == '<' || c == '>' || c == '?';
    }
#endif
}

void press(Key key) {
#ifdef _WIN32
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Key press").context("key", keyToString(key));
        auto it = keyToVirtualKey.find(key);
        if (it != keyToVirtualKey.end()) {
            sendKeyEvent(it->second, true);
        } else {
            BURWELL_HANDLE_ERROR(ErrorType::VALIDATION_ERROR, ErrorSeverity::MEDIUM,
                                  "Unknown key code", keyToString(key), "keyboard::press");
        }
    }, "keyboard::press");
#else
    (void)key;  // Suppress unused parameter warning
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Key press simulated (non-Windows platform)");
    }, "keyboard::press");
#endif
}

void release(Key key) {
#ifdef _WIN32
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Key release").context("key", keyToString(key));
        auto it = keyToVirtualKey.find(key);
        if (it != keyToVirtualKey.end()) {
            sendKeyEvent(it->second, false);
        } else {
            BURWELL_HANDLE_ERROR(ErrorType::VALIDATION_ERROR, ErrorSeverity::MEDIUM,
                                  "Unknown key code", keyToString(key), "keyboard::release");
        }
    }, "keyboard::release");
#else
    (void)key;  // Suppress unused parameter warning
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Key release simulated (non-Windows platform)");
    }, "keyboard::release");
#endif
}

void tap(Key key) {
    press(key);
    std::this_thread::sleep_for(std::chrono::milliseconds(getKeyPressDuration()));
    release(key);
}

void type(const std::string& text) {
#ifdef _WIN32
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Typing text").context("text", text);
        int typingSpeed = getTypingSpeed(); // chars per second
        int delayBetweenChars = (typingSpeed > 0) ? (1000 / typingSpeed) : 50; // Convert to milliseconds
        
        for (char c : text) {
            bool needsShiftKey = needsShift(c);
            if (needsShiftKey) {
                press(Key::SHIFT_LEFT);
                std::this_thread::sleep_for(std::chrono::milliseconds(getKeyReleaseDelay()));
            }
            
            Key key = charToKey(c);
            tap(key);
            
            if (needsShiftKey) {
                release(Key::SHIFT_LEFT);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(delayBetweenChars));
        }
    }, "keyboard::type");
#else
    (void)text;  // Suppress unused parameter warning
    BURWELL_TRY_CATCH({
        SLOG_DEBUG().message("Text typing simulated (non-Windows platform)");
    }, "keyboard::type");
#endif
}

void hotkey(const KeyCombination& combination) {
    BURWELL_TRY_CATCH({
        std::string keyStr = "Hotkey: ";
        for (const auto& key : combination) {
            keyStr += keyToString(key) + "+";
        }
        if (!keyStr.empty()) keyStr.pop_back(); // Remove last '+'
        SLOG_DEBUG().message("Hotkey combination").context("keys", keyStr);
        
        // Press all keys
        for (const auto& key : combination) {
            press(key);
            std::this_thread::sleep_for(std::chrono::milliseconds(getKeyReleaseDelay()));
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(getModifierHoldTime()));
        
        // Release all keys in reverse order
        for (auto it = combination.rbegin(); it != combination.rend(); ++it) {
            release(*it);
            std::this_thread::sleep_for(std::chrono::milliseconds(getKeyReleaseDelay()));
        }
    }, "keyboard::hotkey");
}

void hotkey(std::initializer_list<Key> keys) {
    hotkey(KeyCombination(keys));
}

void pressHotkey(const KeyCombination& combination) {
    for (const auto& key : combination) {
        press(key);
        std::this_thread::sleep_for(std::chrono::milliseconds(getKeyReleaseDelay()));
    }
}

void releaseHotkey(const KeyCombination& combination) {
    for (auto it = combination.rbegin(); it != combination.rend(); ++it) {
        release(*it);
        std::this_thread::sleep_for(std::chrono::milliseconds(getKeyReleaseDelay()));
    }
}

void sendText(const std::string& text, int delayMs) {
    for (char c : text) {
        Key key = charToKey(c);
        tap(key);
        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
    }
}

void sendKeyCode(int virtualKeyCode, bool extended) {
#ifdef _WIN32
    sendKeyEvent(virtualKeyCode, true, extended);
    std::this_thread::sleep_for(std::chrono::milliseconds(getKeyPressDuration()));
    sendKeyEvent(virtualKeyCode, false, extended);
#else
    (void)virtualKeyCode;  // Suppress unused parameter warning
    (void)extended;
#endif
}

void sendScanCode(int scanCode, bool extended) {
#ifdef _WIN32
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scanCode;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &input, sizeof(INPUT));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(getKeyPressDuration()));
    
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
#else
    (void)scanCode;  // Suppress unused parameter warning
    (void)extended;
#endif
}

bool isKeyPressed(Key key) {
#ifdef _WIN32
    auto it = keyToVirtualKey.find(key);
    if (it != keyToVirtualKey.end()) {
        return (GetAsyncKeyState(it->second) & 0x8000) != 0;
    }
#else
    (void)key;  // Suppress unused parameter warning
#endif
    return false;
}

bool isCapsLockOn() {
#ifdef _WIN32
    return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
#endif
    return false;
}

bool isNumLockOn() {
#ifdef _WIN32
    return (GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
#endif
    return false;
}

bool isScrollLockOn() {
#ifdef _WIN32
    return (GetKeyState(VK_SCROLL) & 0x0001) != 0;
#endif
    return false;
}

void logCurrentKeyboardSettings() {
    int typingSpeed = getTypingSpeed();
    int keyPressDuration = getKeyPressDuration();
    int keyReleaseDelay = getKeyReleaseDelay();
    int modifierHoldTime = getModifierHoldTime();
    
    SLOG_DEBUG().message("Keyboard settings from CPL config")
        .context("typing_speed", typingSpeed)
        .context("key_press_duration", keyPressDuration)
        .context("key_release_delay", keyReleaseDelay)
        .context("modifier_hold_time", modifierHoldTime);
}

Key charToKey(char c) {
    char upper = std::toupper(c);
    if (upper >= 'A' && upper <= 'Z') {
        return static_cast<Key>(static_cast<int>(Key::A) + (upper - 'A'));
    }
    if (c >= '0' && c <= '9') {
        return static_cast<Key>(static_cast<int>(Key::NUM_0) + (c - '0'));
    }
    
    switch (c) {
        case ' ': return Key::SPACE;
        case '\n': case '\r': return Key::ENTER;
        case '\t': return Key::TAB;
        case ';': return Key::SEMICOLON;
        case '=': return Key::EQUALS;
        case ',': return Key::COMMA;
        case '-': return Key::MINUS;
        case '.': return Key::PERIOD;
        case '/': return Key::SLASH;
        case '`': return Key::BACKTICK;
        case '[': return Key::LEFT_BRACKET;
        case '\\': return Key::BACKSLASH;
        case ']': return Key::RIGHT_BRACKET;
        case '\'': return Key::QUOTE;
        default: return Key::SPACE; // Fallback
    }
}

std::string keyToString(Key key) {
    static std::map<Key, std::string> keyNames = {
        {Key::A, "A"}, {Key::B, "B"}, {Key::C, "C"}, {Key::D, "D"}, {Key::E, "E"},
        {Key::F, "F"}, {Key::G, "G"}, {Key::H, "H"}, {Key::I, "I"}, {Key::J, "J"},
        {Key::K, "K"}, {Key::L, "L"}, {Key::M, "M"}, {Key::N, "N"}, {Key::O, "O"},
        {Key::P, "P"}, {Key::Q, "Q"}, {Key::R, "R"}, {Key::S, "S"}, {Key::T, "T"},
        {Key::U, "U"}, {Key::V, "V"}, {Key::W, "W"}, {Key::X, "X"}, {Key::Y, "Y"}, {Key::Z, "Z"},
        {Key::NUM_0, "0"}, {Key::NUM_1, "1"}, {Key::NUM_2, "2"}, {Key::NUM_3, "3"}, {Key::NUM_4, "4"},
        {Key::NUM_5, "5"}, {Key::NUM_6, "6"}, {Key::NUM_7, "7"}, {Key::NUM_8, "8"}, {Key::NUM_9, "9"},
        {Key::F1, "F1"}, {Key::F2, "F2"}, {Key::F3, "F3"}, {Key::F4, "F4"},
        {Key::F5, "F5"}, {Key::F6, "F6"}, {Key::F7, "F7"}, {Key::F8, "F8"},
        {Key::F9, "F9"}, {Key::F10, "F10"}, {Key::F11, "F11"}, {Key::F12, "F12"},
        {Key::SPACE, "SPACE"}, {Key::ENTER, "ENTER"}, {Key::TAB, "TAB"},
        {Key::BACKSPACE, "BACKSPACE"}, {Key::DELETE_KEY, "DELETE"}, {Key::ESCAPE, "ESCAPE"},
        {Key::SHIFT_LEFT, "SHIFT_L"}, {Key::SHIFT_RIGHT, "SHIFT_R"},
        {Key::CTRL_LEFT, "CTRL_L"}, {Key::CTRL_RIGHT, "CTRL_R"},
        {Key::ALT_LEFT, "ALT_L"}, {Key::ALT_RIGHT, "ALT_R"},
        {Key::WINDOWS_LEFT, "WIN_L"}, {Key::WINDOWS_RIGHT, "WIN_R"}, {Key::MENU, "MENU"},
        {Key::ARROW_UP, "UP"}, {Key::ARROW_DOWN, "DOWN"},
        {Key::ARROW_LEFT, "LEFT"}, {Key::ARROW_RIGHT, "RIGHT"},
        {Key::HOME, "HOME"}, {Key::END, "END"}, {Key::PAGE_UP, "PAGE_UP"},
        {Key::PAGE_DOWN, "PAGE_DOWN"}, {Key::INSERT, "INSERT"}
    };
    
    auto it = keyNames.find(key);
    return (it != keyNames.end()) ? it->second : "UNKNOWN";
}

// Common shortcuts implementation
namespace shortcuts {
    void copy() { hotkey({Key::CTRL_LEFT, Key::C}); }
    void paste() { hotkey({Key::CTRL_LEFT, Key::V}); }
    void cut() { hotkey({Key::CTRL_LEFT, Key::X}); }
    void undo() { hotkey({Key::CTRL_LEFT, Key::Z}); }
    void redo() { hotkey({Key::CTRL_LEFT, Key::Y}); }
    void selectAll() { hotkey({Key::CTRL_LEFT, Key::A}); }
    void save() { hotkey({Key::CTRL_LEFT, Key::S}); }
    void find() { hotkey({Key::CTRL_LEFT, Key::F}); }
    void newWindow() { hotkey({Key::CTRL_LEFT, Key::N}); }
    void closeWindow() { hotkey({Key::ALT_LEFT, Key::F4}); }
    void switchApp() { hotkey({Key::ALT_LEFT, Key::TAB}); }
    void showDesktop() { hotkey({Key::WINDOWS_LEFT, Key::D}); }
}

} // namespace keyboard
} // namespace ocal