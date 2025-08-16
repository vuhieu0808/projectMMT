#pragma once

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
    // const std::string BASE_DIR = "D:/Hieu/university/1st_year/HK3/MMT/TeamViewer-vuhieu/Project/official/";
    const std::string BASE_DIR = GetCurrentExeDir() + "/";

    const std::string EMAIL_PY_PATH =               BASE_DIR + "client/src/email_code.py";
    const std::string LOG_PATH =                    BASE_DIR + "temp_client/client_log.txt";
    const std::string COMMAND_FILE_PATH =           BASE_DIR + "temp_client/client.txt";
    const std::string EMAIL_CONTENT_PATH =          BASE_DIR + "temp_client/email_content.txt";
    const std::string VIDEO_RECEIVED_PATH =         BASE_DIR + "temp_client/video_received.mp4";
    const std::string KEYLOG_RECEIVED_PATH =        BASE_DIR + "temp_client/keylog_received.txt";
    const std::string PROCESS_LIST_RECEIVED_PATH =  BASE_DIR + "temp_client/process_list_received.txt";
    const std::string DXDIAG_RECEIVED_PATH =        BASE_DIR + "temp_client/dxdiag_received.txt";
    const std::string APP_LIST_RECEIVED_PATH =      BASE_DIR + "temp_client/app_list_received.txt";
    const std::string SCREENSHOT_RECEIVED_PATH =    BASE_DIR + "temp_client/screenshot_received.png";
    const std::string VIDEO_LIST_RECEIVED_PATH =    BASE_DIR + "temp_client/video_list_received.txt";
    const std::string DIRECTORY_LIST_RECEIVED_PATH = BASE_DIR + "temp_client/directory_list_received.txt";

    // Send Mail
    const std::string REDIRECT_URI =                "http://localhost";
    const std::string SCOPE =                       "https://www.googleapis.com/auth/gmail.modify https://www.googleapis.com/auth/gmail.send";
    const std::string TOKEN_FILE =                  BASE_DIR + "config/token.txt";
    const std::string CREDENTIALS_FILE =            BASE_DIR + "config/credentials.json";

    const std::string CACERT =                      BASE_DIR + "lib/cacert/cacert.pem";
}


