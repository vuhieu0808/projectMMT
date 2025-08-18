#include "network.h"
#include "config.h"
#include "utils.h"
#include <fstream>
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace std;

bool SendFile(SOCKET serverSocket, const string& filePath) {
    LogToFile("Attempting to send file: " + filePath);
    ifstream file(filePath, ios::binary | ios::ate);
    if (!file.is_open()) {
        cout << "Cannot open file: " << filePath << endl;
        LogToFile("Cannot open file: " + filePath);
        return false;
    }

    streamsize fileSize = file.tellg();
    file.seekg(0, ios::beg);
    LogToFile("File size: " + to_string(fileSize));

    string sizeStr = to_string(fileSize) + "\n";
    if (send(serverSocket, sizeStr.c_str(), sizeStr.size(), 0) == SOCKET_ERROR) {
        cout << "Failed to send file size: " << WSAGetLastError() << endl;
        LogToFile("Failed to send file size: " + to_string(WSAGetLastError()));
        file.close();
        return false;
    }

    char buffer[8192];
    while (fileSize > 0) {
        file.read(buffer, sizeof(buffer));
        streamsize bytesRead = file.gcount();
        if (send(serverSocket, buffer, bytesRead, 0) == SOCKET_ERROR) {
            cout << "Failed to send file content: " << WSAGetLastError() << endl;
            LogToFile("Failed to send file content: " + to_string(WSAGetLastError()));
            file.close();
            return false;
        }
        fileSize -= bytesRead;
        LogToFile("Sent " + to_string(bytesRead) + " bytes");
    }
    file.close();
    LogToFile("File sent successfully: " + filePath);
    return true;
}

bool ReceiveResponse(SOCKET serverSocket, string& response) {
    char buffer[8192];
    int bytesReceived = recv(serverSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived <= 0) {
        cout << "Failed to receive server response: " << WSAGetLastError() << endl;
        LogToFile("Failed to receive server response: " + to_string(WSAGetLastError()));
        return false;
    }
    buffer[bytesReceived] = '\0';
    response = buffer;
    cout << "Server response: " << response;
    LogToFile("Server response: " + response.substr(0, response.size() - 1));
    return true;
}

bool ReceiveFile(SOCKET serverSocket, const string& fileName, size_t fileSize) {
    LogToFile("Attempting to receive file: " + fileName + " with size: " + to_string(fileSize));

    ofstream file(fileName, ios::binary);
    if (!file.is_open()) {
        cout << "Cannot open file for writing: " << fileName << endl;
        LogToFile("Cannot open file for writing: " + fileName);
        return false;
    }

    char buffer[8192];
    size_t totalBytesReceived = 0;
    while (totalBytesReceived < fileSize) {
        int bytesReceived = recv(serverSocket, buffer, min(sizeof(buffer), static_cast<size_t>(fileSize - totalBytesReceived)), 0);
        if (bytesReceived == 0) {
            cout << "Connection closed by server before receiving complete file" << endl;
            LogToFile("Connection closed by server before receiving complete file");
            file.close();
            return false;
        }
        if (bytesReceived < 0) {
            cout << "Failed to receive file content: " << WSAGetLastError() << endl;
            LogToFile("Failed to receive file content: " + to_string(WSAGetLastError()));
            file.close();
            return false;
        }
        file.write(buffer, bytesReceived);
        file.flush();
        totalBytesReceived += bytesReceived;
        LogToFile("Received " + to_string(bytesReceived) + " bytes, total: " + to_string(totalBytesReceived));
    }
    file.close();

    if (totalBytesReceived == fileSize) {
        cout << "Received file and saved as " << fileName << endl;
        LogToFile("File received successfully: " + fileName + ", total bytes: " + to_string(totalBytesReceived));
        return true;
    } else {
        cout << "Incomplete file received: expected " << fileSize << " bytes, received " << totalBytesReceived << " bytes" << endl;
        LogToFile("Incomplete file received: expected " + to_string(fileSize) + " bytes, received " + to_string(totalBytesReceived));
        return false;
    }
}