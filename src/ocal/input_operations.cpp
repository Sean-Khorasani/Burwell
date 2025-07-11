#include "input_operations.h"
#include "../common/structured_logger.h"
#include "../common/os_utils.h"

namespace ocal {
namespace atomic {
namespace input {

#ifdef _WIN32

bool pressKey(unsigned char key) {
    keybd_event(key, 0, 0, 0);
    SLOG_DEBUG().message("[ATOMIC] Key pressed").context("key", key);
    return true;
}

bool releaseKey(unsigned char key) {
    keybd_event(key, 0, KEYEVENTF_KEYUP, 0);
    SLOG_DEBUG().message("[ATOMIC] Key released").context("key", key);
    return true;
}

bool mouseClick(const std::string& button) {
    DWORD downFlag = 0;
    DWORD upFlag = 0;
    
    if (button == "left") {
        downFlag = MOUSEEVENTF_LEFTDOWN;
        upFlag = MOUSEEVENTF_LEFTUP;
    } else if (button == "right") {
        downFlag = MOUSEEVENTF_RIGHTDOWN;
        upFlag = MOUSEEVENTF_RIGHTUP;
    } else if (button == "middle") {
        downFlag = MOUSEEVENTF_MIDDLEDOWN;
        upFlag = MOUSEEVENTF_MIDDLEUP;
    } else {
        SLOG_ERROR().message("[ATOMIC] Invalid mouse button").context("button", button);
        return false;
    }
    
    mouse_event(downFlag, 0, 0, 0, 0);
    mouse_event(upFlag, 0, 0, 0, 0);
    
    SLOG_DEBUG().message("[ATOMIC] Mouse click performed").context("button", button);
    return true;
}

bool mouseMove(int x, int y) {
    bool success = SetCursorPos(x, y) != FALSE;
    
    if (success) {
        SLOG_DEBUG().message("[ATOMIC] Mouse moved to position").context("x", x).context("y", y);
    } else {
        SLOG_ERROR().message("[ATOMIC] Failed to move mouse");
    }
    
    return success;
}

bool getMousePosition(std::map<std::string, int>& position) {
    POINT mousePos;
    if (GetCursorPos(&mousePos)) {
        position["x"] = mousePos.x;
        position["y"] = mousePos.y;
        SLOG_DEBUG().message("[ATOMIC] Mouse position").context("x", mousePos.x).context("y", mousePos.y);
        return true;
    }
    
    SLOG_ERROR().message("[ATOMIC] Failed to get mouse position");
    return false;
}

bool getClipboard(std::string& text) {
    if (OpenClipboard(NULL)) {
        // Try Unicode first (CF_UNICODETEXT), fallback to ANSI (CF_TEXT)
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData != NULL) {
            wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
            if (pszText != NULL) {
                // Convert Unicode to UTF-8
                text = burwell::os::UnicodeUtils::wideStringToUtf8(pszText);
                GlobalUnlock(hData);
                CloseClipboard();
                SLOG_DEBUG().message("[ATOMIC] Retrieved clipboard text (Unicode)").context("length", text.length());
                return true;
            }
        } else {
            // Fallback to ANSI
            hData = GetClipboardData(CF_TEXT);
            if (hData != NULL) {
                char* pszText = static_cast<char*>(GlobalLock(hData));
                if (pszText != NULL) {
                    text = std::string(pszText);
                    GlobalUnlock(hData);
                    CloseClipboard();
                    SLOG_DEBUG().message("[ATOMIC] Retrieved clipboard text (ANSI)").context("length", text.length());
                    return true;
                }
            }
        }
        CloseClipboard();
    }
    
    SLOG_ERROR().message("[ATOMIC] Failed to get clipboard content");
    return false;
}

bool setClipboard(const std::string& text) {
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        
        // Convert UTF-8 to Unicode
        std::wstring wText = burwell::os::UnicodeUtils::utf8ToWideString(text);
        
        // Set Unicode clipboard data
        size_t len = (wText.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
        if (hMem) {
            memcpy(GlobalLock(hMem), wText.c_str(), len);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
            
            // Also set ANSI version for compatibility
            std::string ansiText = text; // Keep original for ANSI
            size_t ansiLen = ansiText.length() + 1;
            HGLOBAL hAnsiMem = GlobalAlloc(GMEM_MOVEABLE, ansiLen);
            if (hAnsiMem) {
                memcpy(GlobalLock(hAnsiMem), ansiText.c_str(), ansiLen);
                GlobalUnlock(hAnsiMem);
                SetClipboardData(CF_TEXT, hAnsiMem);
            }
            
            CloseClipboard();
            SLOG_DEBUG().message("[ATOMIC] Set clipboard text (Unicode)").context("length", text.length());
            return true;
        }
        CloseClipboard();
    }
    
    SLOG_ERROR().message("[ATOMIC] Failed to set clipboard content");
    return false;
}

#endif // _WIN32

} // namespace input
} // namespace atomic
} // namespace ocal