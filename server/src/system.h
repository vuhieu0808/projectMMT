#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <winsock2.h>

struct Application {
    std::wstring name;
    std::wstring path;
};

std::wstring getExeDirectory();
bool ShutdownReset(SOCKET clientSocket, const std::string& action);
bool ListProcesses(SOCKET clientSocket, const std::wstring& filename);
bool ExportDxDiag(SOCKET clientSocket, const std::wstring& filePath);
bool OpenApplication(SOCKET clientSocket, const std::wstring& appPath);
bool KillProcess(SOCKET clientSocket, DWORD processId);
bool ListApplications(SOCKET clientSocket, const std::wstring& filename);

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
bool CaptureFullScreen(const std::wstring& filename);
bool CaptureScreen(const std::wstring& filename);
