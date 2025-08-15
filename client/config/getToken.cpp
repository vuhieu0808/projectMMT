#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <sstream>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <cstdlib>

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <windows.h>
#define socket_read(fd, buf, count) recv(fd, buf, count, 0)
#define socket_write(fd, buf, count) send(fd, buf, count, 0)
#define socket_close(fd) closesocket(fd)

// Sử dụng nlohmann/json
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Thư viện curl cho HTTP requests
#include <curl/curl.h>

class GmailOAuthClient {
private:
    std::string client_id;
    std::string client_secret;
    std::string redirect_uri;
    std::string state;
    int server_port;

    // Callback để lưu response từ HTTP request
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    // Tạo random state cho bảo mật
    std::string generateRandomState() {
        const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
        
        std::string result;
        for (int i = 0; i < 32; ++i) {
            result += charset[dis(gen)];
        }
        return result;
    }

    // Parse URL parameters
    std::map<std::string, std::string> parseUrlParams(const std::string& query) {
        std::map<std::string, std::string> params;
        std::stringstream ss(query);
        std::string pair;
        
        while (std::getline(ss, pair, '&')) {
            auto pos = pair.find('=');
            if (pos != std::string::npos) {
                std::string key = pair.substr(0, pos);
                std::string value = pair.substr(pos + 1);
                // URL decode (simplified)
                std::replace(value.begin(), value.end(), '+', ' ');
                params[key] = value;
            }
        }
        return params;
    }

    // URL encode
    std::string urlEncode(const std::string& str) {
        CURL* curl = curl_easy_init();
        if (curl) {
            char* encoded = curl_easy_escape(curl, str.c_str(), str.length());
            std::string result(encoded);
            curl_free(encoded);
            curl_easy_cleanup(curl);
            return result;
        }
        return str;
    }

    // Mở trình duyệt
    void openBrowser(const std::string& url) {
        ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }

    // Tạo HTTP server đơn giản để nhận callback
    std::string startCallbackServer() {
        int server_fd;
        struct sockaddr_in address;

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            return "";
        }

        // Tạo socket
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            std::cerr << "Socket creation failed" << std::endl;
            return "";
        }

        // Thiết lập socket options
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) < 0) {
            std::cerr << "Setsockopt failed" << std::endl;
            closesocket(server_fd);
            return "";
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(server_port);

        // Bind socket
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Bind failed" << std::endl;
            closesocket(server_fd);
            return "";
        }

        // Listen
        if (listen(server_fd, 3) < 0) {
            std::cerr << "Listen failed" << std::endl;
            closesocket(server_fd);
            return "";
        }

        std::cout << "Waiting for Google OAuth callback..." << std::endl;

        // Accept connection
        int client_socket;
        int addrlen_size = sizeof(address);
        if ((client_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen_size)) < 0) {
            std::cerr << "Accept failed" << std::endl;
            closesocket(server_fd);
            return "";
        }

        // Đọc HTTP request
        char buffer[4096] = {0};
        int bytes_read = socket_read(client_socket, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            std::cerr << "Cannot read data from client" << std::endl;
            socket_close(client_socket);
            socket_close(server_fd);
            WSACleanup();
            return "";
        }

        std::string request(buffer);
        std::string auth_code = "";

        // Parse HTTP request để lấy authorization code
        auto pos = request.find("GET /?");
        if (pos != std::string::npos) {
            auto end_pos = request.find(" HTTP", pos);
            if (end_pos != std::string::npos) {
                std::string query = request.substr(pos + 6, end_pos - pos - 6);
                auto params = parseUrlParams(query);
                
                if (params.find("code") != params.end() && params.find("state") != params.end()) {
                    if (params["state"] == state) {
                        auth_code = params["code"];
                    }
                }
            }
        }

        // Gửi response về browser
        std::string response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/html\r\n";
        response += "Connection: close\r\n\r\n";
        response += "<html><body>";
        if (!auth_code.empty()) {
            response += "<h2>Authorization successful!</h2>";
            response += "<p>You can close this tab. The application is processing...</p>";
        } else {
            response += "<h2>Authorization failed!</h2>";
            response += "<p>An error occurred during the authorization process.</p>";
        }
        response += "</body></html>";

        int bytes_sent = send(client_socket, response.c_str(), response.length(), 0);
        if (bytes_sent < 0) {
            std::cerr << "Cannot send response" << std::endl;
        }

        // Đóng connections
        closesocket(client_socket);
        closesocket(server_fd);
        WSACleanup();

        return auth_code;
    }

    // Đổi authorization code thành access token
    json exchangeCodeForToken(const std::string& auth_code) {
        CURL* curl;
        CURLcode res;
        std::string response_data;

        curl = curl_easy_init();
        if (curl) {
            // Chuẩn bị POST data
            std::string post_data = "code=" + urlEncode(auth_code) +
                                  "&client_id=" + urlEncode(client_id) +
                                  "&client_secret=" + urlEncode(client_secret) +
                                  "&redirect_uri=" + urlEncode(redirect_uri) +
                                  "&grant_type=authorization_code";

            // Thiết lập curl options
            curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

            // Thực hiện request
            res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);

            if (res == CURLE_OK) {
                try {
                    json json_response = json::parse(response_data);
                    return json_response;
                } catch (const json::parse_error& e) {
                    std::cerr << "JSON parse error: " << e.what() << std::endl;
                    return json::object();
                }
            }
        }

        return json::object();
    }

public:
    GmailOAuthClient() {
        server_port = 8080;
        redirect_uri = "http://localhost:" + std::to_string(server_port);
        state = generateRandomState();
    }

    // Load client credentials từ credentials.json
    bool loadCredentials(const std::string& credentials_file = "credentials.json") {
        try {
            std::ifstream file(credentials_file);
            if (!file.is_open()) {
                std::cerr << "Cannot open file " << credentials_file << std::endl;
                return false;
            }

            json credentials;
            file >> credentials;

            if (credentials.contains("installed")) {
                client_id = credentials["installed"]["client_id"];
                client_secret = credentials["installed"]["client_secret"];
            } else if (credentials.contains("web")) {
                client_id = credentials["web"]["client_id"];
                client_secret = credentials["web"]["client_secret"];
            } else {
                std::cerr << "Format file credentials.json not valid" << std::endl;
                return false;
            }

            return true;
        } catch (const json::parse_error& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            return false;
        } catch (const json::type_error& e) {
            std::cerr << "JSON type error: " << e.what() << std::endl;
            return false;
        }
    }

    // Thực hiện OAuth flow
    bool authenticate() {
        // Tạo authorization URL
        std::string scopes = "https://www.googleapis.com/auth/gmail.modify%20https://www.googleapis.com/auth/gmail.send";
        std::string auth_url = "https://accounts.google.com/o/oauth2/auth"
                              "?response_type=code"
                              "&client_id=" + urlEncode(client_id) +
                              "&redirect_uri=" + urlEncode(redirect_uri) +
                              "&scope=" + scopes +
                              "&state=" + state +
                              "&access_type=offline"
                              "&prompt=consent";

        std::cout << "Opening browser for authentication..." << std::endl;
        std::cout << "URL: " << auth_url << std::endl;

        // Mở trình duyệt
        openBrowser(auth_url);

        // Chạy server callback
        std::string auth_code = startCallbackServer();
        
        if (auth_code.empty()) {
            std::cerr << "Cannot receive authorization code" << std::endl;
            return false;
        }

        std::cout << "Received authorization code, exchanging for tokens..." << std::endl;

        // Đổi code thành tokens
        json token_response = exchangeCodeForToken(auth_code);
        
        if (token_response.empty() || !token_response.contains("access_token")) {
            std::cerr << "Cannot retrieve access token" << std::endl;
            return false;
        }

        // Tạo token.json
        json token_data = {
            {"token", token_response["access_token"]},
            {"token_uri", "https://oauth2.googleapis.com/token"},
            {"client_id", client_id},
            {"client_secret", client_secret},
            {"scopes", {"https://www.googleapis.com/auth/gmail.modify", 
                       "https://www.googleapis.com/auth/gmail.send"}}
        };

        // Thêm refresh_token nếu có
        if (token_response.contains("refresh_token")) {
            token_data["refresh_token"] = token_response["refresh_token"];
        }

        // Lưu vào file token.txt
        std::ofstream token_file("token.txt");
        // token_file << token_data.dump(4); // Pretty print với indent 4

        // std::cout << "Created token.json successfully!" << std::endl;
        // std::cout << "Access Token: " << token_response["access_token"].get<std::string>().substr(0, 50) << "..." << std::endl;
        // if (token_response.contains("refresh_token")) {
        //     std::cout << "Refresh Token: " << token_response["refresh_token"].get<std::string>().substr(0, 50) << "..." << std::endl;
        // }

        std::string access_token = token_response["access_token"].get<std::string>();
        std::string refresh_token = "";
        if (token_response.contains("refresh_token")) {
            refresh_token = token_response["refresh_token"].get<std::string>();
        }
        
        token_file << access_token << '\n' << refresh_token;
        token_file.close();

        return true;
    }
};



int main() {
    std::cout << "=== Gmail OAuth2 Token Generator ===" << std::endl;
    
    // Khởi tạo curl
    curl_global_init(CURL_GLOBAL_DEFAULT);

    GmailOAuthClient client;

    // Load credentials
    if (!client.loadCredentials()) {
        std::cerr << "Error: Cannot load file credentials.json" << std::endl;
        std::cerr << "Please ensure the file credentials.json is in the current directory" << std::endl;
        curl_global_cleanup();
        return 1;
    }

    std::cout << "Loaded credentials.json successfully" << std::endl;

    // Thực hiện authentication
    if (!client.authenticate()) {
        std::cerr << "Authentication process failed" << std::endl;
        curl_global_cleanup();
        return 1;
    }

    std::cout << "\nCompleted! File token.txt has been created." << std::endl;
    std::cout << "Now you can use the Gmail API with this token." << std::endl;

    curl_global_cleanup();
    return 0;
}

// g++ -std=c++11 -Wall -O2 -D_WIN32 getToken.cpp -lcurl -lws2_32 -o getToken.exe