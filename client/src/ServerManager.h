#ifndef SERVER_MANAGER_H
#define SERVER_MANAGER_H

#include <string>
#include <vector>
#include <winsock2.h>

enum class ConnectionStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    CONNECTION_FAILED
};

struct ServerInfo {
    std::string name;
    std::string ip;
    int port;
    
    ServerInfo(const std::string& n, const std::string& i, int p);
    std::string toString() const;
    std::string toFileString() const;
};

class ServerManager {
private:
    std::vector<ServerInfo> serverList;
    int currentServerIndex;
    SOCKET globalClientSocket;
    ConnectionStatus connectionStatus;
    std::string connectionStatusMessage;
    const std::string SERVER_LIST_FILE;

    // Private helper methods
    std::string getConnectionStatusString(ConnectionStatus status) const;
    void setConnectionStatus(ConnectionStatus status, const std::string& message = "");

public:
    ServerManager();
    ~ServerManager();

    // Server list management
    void loadServerList();
    void saveServerList();
    void displayServerList() const;
    void addServer();
    void removeServer();
    bool selectServer();

    // Connection management
    bool connectToSelectedServer();
    bool disconnectFromServer();
    void displayConnectionStatus() const;
    
    // Getters
    bool isConnected() const;
    SOCKET getSocket() const;
    int getCurrentServerIndex() const;
    ConnectionStatus getConnectionStatus() const;
    std::string getConnectionStatusMessage() const;
    const std::vector<ServerInfo>& getServerList() const;
    
    // Menu and interaction
    void manageServersMenu();
    
    // Utility methods
    bool hasServers() const;
    bool hasSelectedServer() const;
    std::string getCurrentServerString() const;
};

#endif // SERVER_MANAGER_H