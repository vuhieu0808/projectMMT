#pragma once

#include <bits/stdc++.h>
#include <string>
#include <atomic>
#include <filesystem>

namespace fs = std::filesystem;

std::string wstring_to_string(const std::wstring& wstr);
std::wstring string_to_wstring(const std::string& str);
std::wstring convert_backslashes_to_slashes(std::wstring path);
void LogToFile(const std::string& message);
std::wstring getExeDirectory();

extern std::atomic<bool> isRunning;