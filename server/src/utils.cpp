#include "utils.h"
#include "config.h"
#include <fstream>
#include <windows.h>

std::atomic<bool> isRunning(false);

std::string wstring_to_string(const std::wstring& wstr) {
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

std::wstring string_to_wstring(const std::string& str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

std::wstring convert_backslashes_to_slashes(std::wstring path) {
    std::replace(path.begin(), path.end(), L'\\', L'/');
    return path;
}

void LogToFile(const std::string& message) {
    std::ofstream logFile(Config::logPath, std::ios_base::app);
    if (logFile.is_open()) {
        logFile << message << std::endl;
        logFile.close();
    }
}

std::wstring getExeDirectory() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
    return std::wstring(buffer).substr(0, pos + 1);
}