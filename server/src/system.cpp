#include "system.h"
#include "utils.h"

#include <tlhelp32.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <codecvt>
#include <gdiplus.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// Shutdown - Reset
bool ShutdownReset(SOCKET clientSocket, const std::string& action) {
    LogToFile("Received shutdown/reset command: " + action);
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LogToFile("OpenProcessToken failed: " + std::to_string(GetLastError()));
        std::string response = "ERROR: Cannot adjust privileges\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return false;
    }

    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, NULL);
    if (GetLastError() != ERROR_SUCCESS) {
        LogToFile("AdjustTokenPrivileges failed: " + std::to_string(GetLastError()));
        CloseHandle(hToken);
        std::string response = "ERROR: Cannot adjust privileges\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return false;
    }

    bool success = false;
    if (action == "shutdown") {
        success = ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, 0);
    } else if (action == "reset") {
        success = ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0);
    } else {
        LogToFile("Invalid shutdown/reset action: " + action);
        std::string response = "ERROR: Invalid action (use shutdown or reset)\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        CloseHandle(hToken);
        return false;
    }

    CloseHandle(hToken);
    if (!success) {
        LogToFile("Shutdown/reset failed: " + std::to_string(GetLastError()));
        std::string response = "ERROR: Failed to " + action + "\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return false;
    }

    LogToFile(action + " command executed successfully");
    std::string response = "System will " + action + "\n";
    send(clientSocket, response.c_str(), response.size(), 0);
    return true;
}

bool ListProcesses(SOCKET clientSocket, const std::wstring& filename) {
    LogToFile("Listing processes and applications");
    std::vector<std::pair<std::wstring, DWORD>> openApplications; // Store both title and PID
    auto EnumWindowsProc = [](HWND hwnd, LPARAM lParam) -> BOOL {
        const DWORD TITLE_SIZE = 1024;
        WCHAR windowTitle[TITLE_SIZE];
        auto* apps = reinterpret_cast<std::vector<std::pair<std::wstring, DWORD>>*>(lParam);
        if (IsWindowVisible(hwnd)) {
            int length = GetWindowTextW(hwnd, windowTitle, TITLE_SIZE);
            if (length > 0) {
                DWORD processId;
                GetWindowThreadProcessId(hwnd, &processId); // Get PID for the window
                apps->push_back({windowTitle, processId});
            }
        }
        return TRUE;
    };

    openApplications.clear();
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&openApplications));

    HANDLE hFile = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        LogToFile("Failed to create file: " + wstring_to_string(filename));
        std::string response = "ERROR: Cannot create process list file\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return false;
    }

    const wchar_t bom = 0xFEFF;
    const wchar_t newline = L'\n';
    DWORD bytesWritten;
    WriteFile(hFile, &bom, sizeof(bom), &bytesWritten, NULL);

    std::wstring header = L"List Of Applications:\n";
    WriteFile(hFile, header.c_str(), header.size() * sizeof(wchar_t), &bytesWritten, NULL);
    for (const auto& app : openApplications) {
        std::wstring appInfo = L"PID: " + std::to_wstring(app.second) + L"\tTitle: " + app.first + L"\n";
        WriteFile(hFile, appInfo.c_str(), appInfo.size() * sizeof(wchar_t), &bytesWritten, NULL);
    }

    // processes
    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        LogToFile("CreateToolhelp32Snapshot failed: " + std::to_string(GetLastError()));
        CloseHandle(hFile);
        std::string response = "ERROR: Cannot get process snapshot\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return false;
    }

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    if (!Process32FirstW(hProcessSnap, &pe32)) {
        LogToFile("Process32First failed: " + std::to_string(GetLastError()));
        CloseHandle(hProcessSnap);
        CloseHandle(hFile);
        std::string response = "ERROR: Cannot list processes\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return false;
    }

    WriteFile(hFile, &newline, sizeof(newline), &bytesWritten, NULL);
    header = L"List Of Processes:\n";
    WriteFile(hFile, header.c_str(), header.size() * sizeof(wchar_t), &bytesWritten, NULL);

    do {
        std::wstring processInfo = L"PID: " + std::to_wstring(pe32.th32ProcessID) + L"\tName: " + pe32.szExeFile + L"\n";
        WriteFile(hFile, processInfo.c_str(), processInfo.size() * sizeof(wchar_t), &bytesWritten, NULL);
    } while (Process32NextW(hProcessSnap, &pe32));

    CloseHandle(hProcessSnap);
    CloseHandle(hFile);
    LogToFile("Process list saved to: " + wstring_to_string(filename));
    std::string response = "Process list generated, sending " + wstring_to_string(filename) + "\n";
    send(clientSocket, response.c_str(), response.size(), 0);
    return true;
}

bool ExportDxDiag(SOCKET clientSocket, const std::wstring& filePath) {
    LogToFile("Running dxdiag to export to: " + wstring_to_string(filePath));
    std::wstring command = L"dxdiag /t \"" + filePath + L"\"";
    int result = _wsystem(command.c_str());
    if (result == 0) {
        LogToFile("Dxdiag exported successfully");
        std::string response = "Dxdiag exported, sending " + wstring_to_string(filePath) + "\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return true;
    } else {
        LogToFile("Dxdiag export failed");
        std::string response = "ERROR: Failed to export dxdiag\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return false;
    }
}

bool OpenApplication(SOCKET clientSocket, const std::wstring& appPath) {
    LogToFile("Attempting to open application: " + wstring_to_string(appPath));
    HINSTANCE result = ShellExecuteW(NULL, L"open", appPath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
    if ((int)(INT_PTR)result > 32) {
        LogToFile("Application opened: " + wstring_to_string(appPath));
        std::string response = "Application opened: " + wstring_to_string(appPath) + "\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return true;
    } else {
        LogToFile("Failed to open application: " + wstring_to_string(appPath));
        std::string response = "ERROR: Failed to open application: " + wstring_to_string(appPath) + "\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return false;
    }
}

bool KillProcess(SOCKET clientSocket, DWORD processId) {
    LogToFile("Attempting to kill process with ID: " + std::to_string(processId));
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
    if (hProcess == NULL) {
        LogToFile("OpenProcess failed: " + std::to_string(GetLastError()));
        std::string response = "ERROR: Cannot open process ID " + std::to_string(processId) + "\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return false;
    }

    bool result = TerminateProcess(hProcess, 1);
    CloseHandle(hProcess);
    if (result) {
        LogToFile("Process killed: ID " + std::to_string(processId));
        std::string response = "Process killed: ID " + std::to_string(processId) + "\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return true;
    } else {
        LogToFile("Failed to kill process: ID " + std::to_string(processId));
        std::string response = "ERROR: Failed to kill process ID " + std::to_string(processId) + "\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return false;
    }
}

// List applications can open with processID
bool ListApplications(SOCKET clientSocket, const std::wstring& filename) {
    LogToFile("Listing system applications");
    std::vector<Application> apps;
    std::set<std::wstring> uniquePaths;

    // Scan system directories
    const wchar_t* systemPaths[] = {
        L"C:\\Windows\\System32",
        L"C:\\Windows",
        L"C:\\Program Files",
        L"C:\\Program Files (x86)"
    };
    for (const auto& dir : systemPaths) {
        WIN32_FIND_DATAW findData;
        std::wstring searchPath = std::wstring(dir) + L"\\*.exe";
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring appName = findData.cFileName;
                    appName = appName.substr(0, appName.find_last_of(L'.'));
                    std::wstring appPath = std::wstring(dir) + L"\\" + findData.cFileName;
                    apps.push_back({appName, appPath});
                }
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
    }

    // Scan registry
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t subkeyName[MAX_PATH];
        DWORD subkeyNameSize = MAX_PATH;
        for (DWORD i = 0; ; i++) {
            if (RegEnumKeyExW(hKey, i, subkeyName, &subkeyNameSize, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            std::wstring appKeyPath = std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\") + subkeyName;
            HKEY hAppKey;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, appKeyPath.c_str(), 0, KEY_READ, &hAppKey) == ERROR_SUCCESS) {
                wchar_t appPath[MAX_PATH];
                DWORD appPathSize = sizeof(appPath);
                if (RegQueryValueExW(hAppKey, NULL, NULL, NULL, (LPBYTE)appPath, &appPathSize) == ERROR_SUCCESS) {
                    std::wstring appName = subkeyName;
                    appName = appName.substr(0, appName.find_last_of(L'.'));
                    apps.push_back({appName, appPath});
                }
                RegCloseKey(hAppKey);
            }
            subkeyNameSize = MAX_PATH;
        }
        RegCloseKey(hKey);
    }

    // Scan Start Menu
    wchar_t startMenuPath[MAX_PATH];
    if (SHGetSpecialFolderPathW(NULL, startMenuPath, CSIDL_COMMON_PROGRAMS, FALSE)) {
        WIN32_FIND_DATAW findData;
        std::wstring searchPath = std::wstring(startMenuPath) + L"\\*";
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0) {
                    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        std::wstring subDirPath = std::wstring(startMenuPath) + L"\\" + findData.cFileName;
                        std::wstring subDirSearchPath = subDirPath + L"\\*.lnk";
                        HANDLE hSubFind = FindFirstFileW(subDirSearchPath.c_str(), &findData);
                        if (hSubFind != INVALID_HANDLE_VALUE) {
                            do {
                                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                                    std::wstring appName = findData.cFileName;
                                    appName = appName.substr(0, appName.find_last_of(L'.'));
                                    std::wstring appPath = subDirPath + L"\\" + findData.cFileName;
                                    apps.push_back({appName, appPath});
                                }
                            } while (FindNextFileW(hSubFind, &findData));
                            FindClose(hSubFind);
                        }
                    } else if (wcsstr(findData.cFileName, L".lnk")) {
                        std::wstring appName = findData.cFileName;
                        appName = appName.substr(0, appName.find_last_of(L'.'));
                        std::wstring appPath = std::wstring(startMenuPath) + L"\\" + findData.cFileName;
                        apps.push_back({appName, appPath});
                    }
                }
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
    }

    // Remove duplicates and sort
    std::vector<Application> uniqueApps;
    for (const auto& app : apps) {
        if (uniquePaths.find(app.path) == uniquePaths.end()) {
            uniquePaths.insert(app.path);
            uniqueApps.push_back(app);
        }
    }
    std::sort(uniqueApps.begin(), uniqueApps.end(), 
        [](const Application& a, const Application& b) { return a.name < b.name; });

    // Write to file
    std::wofstream outFile(wstring_to_string(filename));
    outFile.imbue(std::locale(outFile.getloc(), new std::codecvt_utf8<wchar_t>()));
    if (outFile.is_open()) {
        outFile << L"DANH SÁCH TẤT CẢ ỨNG DỤNG HỆ THỐNG (" << uniqueApps.size() << L" ứng dụng):\n";
        for (size_t i = 0; i < uniqueApps.size(); ++i) {
            std::wstring res = std::to_wstring(i + 1) + L". " + uniqueApps[i].name + L"\n" + L"   " + convert_backslashes_to_slashes(uniqueApps[i].path) + L"\n";
            outFile << res;
        }
        outFile.close();
        LogToFile("Application list saved to: " + wstring_to_string(filename));
        std::string response = "Application list generated, sending " + wstring_to_string(filename) + "\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return true;
    } else {
        LogToFile("Failed to open file for writing: " + wstring_to_string(filename));
        std::string response = "ERROR: Cannot open file for writing\n";
        send(clientSocket, response.c_str(), response.size(), 0);
        return false;
    }
}


using namespace Gdiplus;

// Hàm lấy encoder CLSID cho PNG
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
    UINT num = 0;
    UINT size = 0;

    ImageCodecInfo* pImageCodecInfo = NULL;

    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;

    GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j)
    {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
        {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }

    free(pImageCodecInfo);
    return -1;
}

// Hàm chụp toàn bộ màn hình (bao gồm multiple monitors)
bool CaptureFullScreen(const std::wstring& filename)
{
    // Thiết lập DPI awareness để lấy độ phân giải thực tế
    SetProcessDPIAware();

    // Lấy kích thước virtual screen (bao gồm tất cả monitors)
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // Nếu không có virtual screen, sử dụng primary screen
    if (width == 0 || height == 0) {
        x = y = 0;
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }

    LogToFile("Kich thuoc capture: " + std::to_string(width) + "x" + std::to_string(height) + " pixels");
    LogToFile("Vi tri bat dau: (" + std::to_string(x) + ", " + std::to_string(y) + ")");

    // Tạo device contexts
    HDC hScreenDC = CreateDC("DISPLAY", NULL, NULL, NULL);
    if (!hScreenDC) {
        LogToFile("Loi: Khong the tao Screen DC!");
        return false;
    }

    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) {
        LogToFile("Loi: Khong the tao Memory DC!");
        DeleteDC(hScreenDC);
        return false;
    }

    // Tạo bitmap với kích thước thực tế
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    if (!hBitmap) {
        LogToFile("Loi: Khong the tao Bitmap!");
        DeleteDC(hMemoryDC);
        DeleteDC(hScreenDC);
        return false;
    }

    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    // Capture màn hình với CAPTUREBLT để bao gồm layered windows
    BOOL result = BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, x, y, SRCCOPY | CAPTUREBLT);
    
    if (!result) {
        LogToFile("Loi BitBlt! Ma loi: " + std::to_string(GetLastError()));
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        DeleteDC(hScreenDC);
        return false;
    }

    // Tạo GDI+ Bitmap từ HBITMAP
    Bitmap* bitmap = Bitmap::FromHBITMAP(hBitmap, NULL);
    if (!bitmap) {
        LogToFile("Loi: Khong the tao GDI+ Bitmap!");
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        DeleteDC(hScreenDC);
        return false;
    }

    LogToFile("Kich thuoc bitmap: " + std::to_string(bitmap->GetWidth()) + "x" + std::to_string(bitmap->GetHeight()) + " pixels");

    // Lấy PNG encoder
    CLSID pngClsid;
    if (GetEncoderClsid(L"image/png", &pngClsid) == -1) {
        LogToFile("Loi: Khong tim thay PNG encoder!");
        delete bitmap;
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        DeleteDC(hScreenDC);
        return false;
    }

    // Lưu file PNG
    Status status = bitmap->Save(filename.c_str(), &pngClsid, NULL);
    
    // Dọn dẹp bộ nhớ
    delete bitmap;
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    DeleteDC(hScreenDC);

    if (status != Ok) {
        LogToFile("Loi luu file! GDI+ Status: " + std::to_string(status));
        return false;
    }

    return true;
}

bool CaptureScreen(const std::wstring& filename) {
    // Khởi tạo GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Status gdiStatus = GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    if (gdiStatus != Ok) {
        LogToFile("Loi khoi tao GDI+!");
        return false;
    }

    // Hiển thị thông tin màn hình
    int primaryWidth = GetSystemMetrics(SM_CXSCREEN);
    int primaryHeight = GetSystemMetrics(SM_CYSCREEN);
    int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    LogToFile("Man hinh chinh: " + std::to_string(primaryWidth) + "x" + std::to_string(primaryHeight) + " pixels");
    if (virtualWidth > 0 && virtualHeight > 0) {
        LogToFile("Toan bo man hinh: " + std::to_string(virtualWidth) + "x" + std::to_string(virtualHeight) + " pixels");
    }

    LogToFile("\n--- BAT DAU CHUP MAN HINH ---");
    LogToFile("Dang xu ly...");

    // Thực hiện chụp màn hình
    bool success = CaptureFullScreen(filename);

    LogToFile("\n--- KET QUA ---");
    if (success) {
        LogToFile("THANH CONG! File da luu: " + wstring_to_string(filename));
        LogToFile("Ban co the mo file de xem anh chup man hinh.");
    } else {
        LogToFile("THAT BAI! Khong the chup man hinh.");
        LogToFile("Thu chay voi quyen Administrator hoac kiem tra lai.");
    }

    // Dọn dẹp GDI+
    GdiplusShutdown(gdiplusToken);
    return success;
}