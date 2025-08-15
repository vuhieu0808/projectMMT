#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>

std::pair<std::string, std::string> ReadCommandFromFile(const std::string& fileName);
bool SendFile(SOCKET clientSocket, const std::wstring& filePath);
bool ReceiveFile(SOCKET clientSocket, const std::string& fileName);
void HandleClient(SOCKET clientSocket);
void ServerThread();