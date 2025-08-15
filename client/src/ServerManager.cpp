#include "ServerManager.h"
#include "config.h"
#include "utils.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <ws2tcpip.h>

using namespace std;

// ServerInfo implementation
ServerInfo::ServerInfo(const string& n, const string& i, int p) : name(n), ip(i), port(p) {}

string ServerInfo::toString() const {
    return name + " (" + ip + ":" + to_string(port) + ")";
}

string ServerInfo::toFileString() const {
    return name + "|" + ip + "|" + to_string(port);
}

// ServerManager implementation
ServerManager::ServerManager() 
    : currentServerIndex(-1)
    , globalClientSocket(INVALID_SOCKET)
    , connectionStatus(ConnectionStatus::DISCONNECTED)
    , SERVER_LIST_FILE(Config::BASE_DIR + "temp_client/servers.txt") {
    loadServerList();
}

ServerManager::~ServerManager() {
    if (isConnected()) {
        disconnectFromServer();
    }
}

string ServerManager::getConnectionStatusString(ConnectionStatus status) const {
    switch (status) {
        case ConnectionStatus::DISCONNECTED:
            return "DISCONNECTED";
        case ConnectionStatus::CONNECTING:
            return "CONNECTING...";
        case ConnectionStatus::CONNECTED:
            return "CONNECTED";
        case ConnectionStatus::CONNECTION_FAILED:
            return "CONNECTION FAILED";
        default:
            return "UNKNOWN";
    }
}

void ServerManager::setConnectionStatus(ConnectionStatus status, const string& message) {
    connectionStatus = status;
    connectionStatusMessage = message;
    LogToFile("Connection status changed to: " + getConnectionStatusString(status) + 
              (message.empty() ? "" : " - " + message));
}

void ServerManager::loadServerList() {
    serverList.clear();
    ifstream file(SERVER_LIST_FILE);
    if (!file.is_open()) {
        // Create default servers if file doesn't exist
        serverList.push_back(ServerInfo("Local Server", "127.0.0.1", 12345));
        saveServerList();
        return;
    }
    
    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;
        
        size_t pos1 = line.find('|');
        size_t pos2 = line.find('|', pos1 + 1);
        
        if (pos1 != string::npos && pos2 != string::npos) {
            string name = line.substr(0, pos1);
            string ip = line.substr(pos1 + 1, pos2 - pos1 - 1);
            string portStr = line.substr(pos2 + 1);
            
            try {
                int port = stoi(portStr);
                serverList.push_back(ServerInfo(name, ip, port));
            } catch (...) {
                // Skip invalid lines
            }
        }
    }
    file.close();
    
    if (serverList.empty()) {
        serverList.push_back(ServerInfo("Local Server", "127.0.0.1", 12345));
    }
}

void ServerManager::saveServerList() {
    ofstream file(SERVER_LIST_FILE);
    if (file.is_open()) {
        for (const auto& server : serverList) {
            file << server.toFileString() << endl;
        }
        file.close();
        LogToFile("Server list saved to " + SERVER_LIST_FILE);
    } else {
        LogToFile("Failed to save server list to " + SERVER_LIST_FILE);
    }
}

void ServerManager::displayServerList() const {
    cout << "\n========== SERVER LIST ==========\n";
    if (serverList.empty()) {
        cout << "No servers configured.\n";
    } else {
        for (size_t i = 0; i < serverList.size(); ++i) {
            cout << (i + 1) << ". " << serverList[i].toString();
            if (static_cast<int>(i) == currentServerIndex) {
                cout << " [CURRENT]";
            }
            cout << endl;
        }
    }
    cout << "================================\n";
}

void ServerManager::addServer() {
    cout << "\n--- Add New Server ---\n";
    cout << "Enter server name: ";
    string name;
    getline(cin, name);
    
    if (name.empty()) {
        cout << "Server name cannot be empty." << endl;
        return;
    }
    
    cout << "Enter server IP: ";
    string ip;
    getline(cin, ip);
    
    if (ip.empty()) {
        cout << "Server IP cannot be empty." << endl;
        return;
    }
    
    cout << "Enter server port: ";
    string portStr;
    getline(cin, portStr);
    
    try {
        int port = stoi(portStr);
        if (port <= 0 || port > 65535) {
            cout << "Invalid port number. Must be between 1-65535." << endl;
            return;
        }
        
        serverList.push_back(ServerInfo(name, ip, port));
        saveServerList();
        cout << "Server added successfully: " << name << " (" << ip << ":" << port << ")" << endl;
        LogToFile("Added server: " + name + " (" + ip + ":" + to_string(port) + ")");
    } catch (...) {
        cout << "Invalid port number." << endl;
    }
}

void ServerManager::removeServer() {
    if (serverList.empty()) {
        cout << "No servers to remove." << endl;
        return;
    }
    
    displayServerList();
    cout << "Enter server number to remove (1-" << serverList.size() << "): ";
    string input;
    getline(cin, input);
    
    try {
        int index = stoi(input) - 1;
        if (index < 0 || index >= static_cast<int>(serverList.size())) {
            cout << "Invalid server number." << endl;
            return;
        }
        
        // Disconnect if removing current server
        if (currentServerIndex == index && connectionStatus == ConnectionStatus::CONNECTED) {
            disconnectFromServer();
            cout << "Disconnected from server (server being removed)." << endl;
        }
        
        string removedServer = serverList[index].toString();
        serverList.erase(serverList.begin() + index);
        
        // Adjust current server index
        if (currentServerIndex == index) {
            currentServerIndex = -1;
        } else if (currentServerIndex > index) {
            currentServerIndex--;
        }
        
        saveServerList();
        cout << "Server removed: " << removedServer << endl;
        LogToFile("Removed server: " + removedServer);
    } catch (...) {
        cout << "Invalid input." << endl;
    }
}

bool ServerManager::selectServer() {
    if (serverList.empty()) {
        cout << "No servers available. Please add a server first." << endl;
        return false;
    }
    
    displayServerList();
    cout << "Enter server number to select (1-" << serverList.size() << "): ";
    string input;
    getline(cin, input);
    
    try {
        int index = stoi(input) - 1;
        if (index < 0 || index >= static_cast<int>(serverList.size())) {
            cout << "Invalid server number." << endl;
            return false;
        }
        
        // Disconnect from current server if connected and switching to different server
        if (connectionStatus == ConnectionStatus::CONNECTED && currentServerIndex != index) {
            disconnectFromServer();
            cout << "Disconnected from previous server." << endl;
        }
        
        currentServerIndex = index;
        cout << "Selected server: " << serverList[currentServerIndex].toString() << endl;
        LogToFile("Selected server: " + serverList[currentServerIndex].toString());
        return true;
    } catch (...) {
        cout << "Invalid input." << endl;
        return false;
    }
}

bool ServerManager::connectToSelectedServer() {
    if (currentServerIndex < 0) {
        cout << "No server selected. Please select a server first." << endl;
        return false;
    }
    
    if (connectionStatus == ConnectionStatus::CONNECTED) {
        cout << "Already connected to " << serverList[currentServerIndex].toString() << endl;
        return true;
    }
    
    const ServerInfo& server = serverList[currentServerIndex];
    cout << "Connecting to " << server.toString() << "..." << endl;
    setConnectionStatus(ConnectionStatus::CONNECTING);
    
    // Create socket
    globalClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (globalClientSocket == INVALID_SOCKET) {
        string error = "Socket creation failed: " + to_string(WSAGetLastError());
        cout << error << endl;
        setConnectionStatus(ConnectionStatus::CONNECTION_FAILED, error);
        return false;
    }

    // Server address
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(server.port);
    if (inet_pton(AF_INET, server.ip.c_str(), &serverAddr.sin_addr) <= 0) {
        string error = "Invalid server IP address: " + server.ip;
        cout << error << endl;
        setConnectionStatus(ConnectionStatus::CONNECTION_FAILED, error);
        closesocket(globalClientSocket);
        globalClientSocket = INVALID_SOCKET;
        return false;
    }

    // Connect to server
    if (connect(globalClientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        string error = "Connection failed: " + to_string(WSAGetLastError());
        cout << error << endl;
        setConnectionStatus(ConnectionStatus::CONNECTION_FAILED, error);
        closesocket(globalClientSocket);
        globalClientSocket = INVALID_SOCKET;
        return false;
    }

    cout << "Successfully connected to " << server.toString() << endl;
    setConnectionStatus(ConnectionStatus::CONNECTED, "Connection established");
    return true;
}

bool ServerManager::disconnectFromServer() {
    if (connectionStatus != ConnectionStatus::CONNECTED) {
        cout << "Not connected to any server." << endl;
        return false;
    }
    
    if (globalClientSocket != INVALID_SOCKET) {
        closesocket(globalClientSocket);
        globalClientSocket = INVALID_SOCKET;
    }
    
    cout << "Disconnected from server." << endl;
    setConnectionStatus(ConnectionStatus::DISCONNECTED, "User disconnected");
    return true;
}

void ServerManager::displayConnectionStatus() const {
    cout << "\n========== CONNECTION STATUS ==========\n";
    if (currentServerIndex >= 0) {
        cout << "Selected Server: " << serverList[currentServerIndex].toString() << endl;
        cout << "Connection Status: " << getConnectionStatusString(connectionStatus);
        if (!connectionStatusMessage.empty()) {
            cout << " - " << connectionStatusMessage;
        }
        cout << endl;
    } else {
        cout << "Selected Server: None" << endl;
        cout << "Connection Status: No server selected" << endl;
    }
    cout << "=====================================\n";
}

void ServerManager::manageServersMenu() {
    while (true) {
        system("cls");
        cout << "\n========== SERVER MANAGEMENT ==========\n";
        displayServerList();
        displayConnectionStatus();
        cout << "\n1. Add Server\n";
        cout << "2. Remove Server\n";
        cout << "3. Select Server\n";
        cout << "4. Connect to Selected Server\n";
        cout << "5. Disconnect from Server\n";
        cout << "6. Back to Main Menu\n";
        cout << "======================================\n";
        cout << "Choose option: ";
        
        string input;
        getline(cin, input);
        
        if (input.empty()) continue;
        
        int choice;
        try {
            choice = stoi(input);
        } catch (...) {
            cout << "Invalid option. Please try again." << endl;
            cout << "\nPress Enter to continue...";
            cin.get();
            continue;
        }
        
        switch (choice) {
            case 1:
                addServer();
                break;
            case 2:
                removeServer();
                break;
            case 3:
                selectServer();
                break;
            case 4:
                connectToSelectedServer();
                break;
            case 5:
                disconnectFromServer();
                break;
            case 6:
                return;
            default:
                cout << "Invalid option. Please try again." << endl;
        }
        
        if (choice != 6) {
            cout << "\nPress Enter to continue...";
            cin.get();
        }
    }
}

// Getters
bool ServerManager::isConnected() const {
    return connectionStatus == ConnectionStatus::CONNECTED;
}

SOCKET ServerManager::getSocket() const {
    return globalClientSocket;
}

int ServerManager::getCurrentServerIndex() const {
    return currentServerIndex;
}

ConnectionStatus ServerManager::getConnectionStatus() const {
    return connectionStatus;
}

string ServerManager::getConnectionStatusMessage() const {
    return connectionStatusMessage;
}

const vector<ServerInfo>& ServerManager::getServerList() const {
    return serverList;
}

bool ServerManager::hasServers() const {
    return !serverList.empty();
}

bool ServerManager::hasSelectedServer() const {
    return currentServerIndex >= 0 && currentServerIndex < static_cast<int>(serverList.size());
}

string ServerManager::getCurrentServerString() const {
    if (hasSelectedServer()) {
        return serverList[currentServerIndex].toString();
    }
    return "None selected";
}