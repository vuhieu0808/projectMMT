#pragma once

#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace std;

bool SendFile(SOCKET serverSocket, const string& filePath);
bool ReceiveResponse(SOCKET serverSocket, string& response);
bool isValidNumber(const string& str);
bool ReceiveFile(SOCKET serverSocket, const string& fileName, size_t fileSize);
