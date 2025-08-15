#pragma once

#include "utils.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define byte win_byte_override
#include <windows.h>
#include <shlwapi.h>
#undef byte

#include <string>

inline std::string GetCurrentExeDir() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    PathRemoveFileSpecA(path); // bỏ phần tên file exe, giữ lại thư mục
    return std::string(path);
}

namespace Config {
// Base directory for temporary files
// const std::wstring BASE_DIR = L"D:\\Hieu\\university\\1st_year\\HK3\\MMT\\TeamViewer-vuhieu\\Project\\official\\temp_server\\";
const std::wstring BASE_DIR = string_to_wstring(GetCurrentExeDir()) + L"\\temp_server\\";

// File paths
const std::string CLIENT_FILE =             wstring_to_string(BASE_DIR) + "client.txt";
const std::wstring KEYLOG_FILE =            BASE_DIR + L"keylog.txt";
const std::wstring PROCESS_LIST_FILE =      BASE_DIR + L"listProcess.txt";
const std::wstring APPLICATION_LIST_FILE =  BASE_DIR + L"listApplication.txt";
const std::wstring DXDIAG_FILE =            BASE_DIR + L"dxdiag_output.txt";
const std::wstring VIDEO_FILE =             BASE_DIR + L"video.mp4";
const std::string logPath =                 wstring_to_string(BASE_DIR) + "server_log.txt";
const std::wstring SCREENSHOT_FILE =        BASE_DIR + L"screenshot.png";

} // namespace Config