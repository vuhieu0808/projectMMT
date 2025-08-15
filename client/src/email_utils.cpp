#define WIN32_LEAN_AND_MEAN

#include "email_utils.h"
#include "utils.h"

#include <iostream>
#include <fstream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

bool ReadEmailContent(std::string& ipPort, std::string& command, std::string& fromEmail, const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cout << "Cannot open email content file: " << filePath << std::endl;
        LogToFile("Cannot open email content file: " + filePath);
        return false;
    }

    std::string line;
    std::getline(file, line); // IP:port
    if (line.empty()) {
        file.close();
        return false;
    }
    ipPort = line;
    std::getline(file, command); // Command
    std::getline(file, fromEmail); // From email
    while (ipPort.back() == ' ' || ipPort.back() == '\n') ipPort.pop_back();
    while (command.back() == ' ' || command.back() == '\n') command.pop_back();
    while (fromEmail.back() == ' ' || fromEmail.back() == '\n') fromEmail.pop_back();
    file.close();
    fs::remove(filePath);
    return !ipPort.empty() && !command.empty() && !fromEmail.empty();
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append(static_cast<char*>(contents), newLength);
    } catch (std::bad_alloc& e) {
        return 0;
    }
    return newLength;
}

std::string sendRequest(const std::string& url, const std::string& postData) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;

    if (curl) {
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_CAINFO, Config::CACERT.c_str());

        if (!postData.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postData.length());
        }

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            LogToFile("curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)));
            readBuffer.clear();
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

bool isAccessTokenValid(const std::string& access_token) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize curl\n";
        LogToFile("Failed to initialize curl");
        return false;
    }

    std::string response;
    std::string url = "https://www.googleapis.com/oauth2/v1/tokeninfo?access_token=" + access_token;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CAINFO, Config::CACERT.c_str());

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        LogToFile("curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)));
    }

    curl_easy_cleanup(curl);
    return res == CURLE_OK && response.find("expires_in") != std::string::npos;
}

bool readCredentials(const std::string& filename, std::string& client_id, std::string& client_secret) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open credentials.json" << std::endl;
        LogToFile("Failed to open credentials.json");
        return false;
    }

    try {
        json j;
        file >> j;
        if (j.contains("installed")) {
            auto installed = j["installed"];
            client_id = installed["client_id"].get<std::string>();
            client_secret = installed["client_secret"].get<std::string>();
            return true;
        } else {
            std::cerr << "Invalid credentials.json format: 'installed' section not found" << std::endl;
            LogToFile("Invalid credentials.json format: 'installed' section not found");
            return false;
        }
    } catch (const json::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        LogToFile("JSON parse error: " + std::string(e.what()));
        return false;
    }
}

bool readToken(const std::string& filename, std::string& access_token, std::string& refresh_token) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open token.txt" << std::endl;
        LogToFile("Failed to open token.txt");
        return false;
    }
    std::getline(file, access_token);
    std::getline(file, refresh_token);
    file.close();
    return !access_token.empty();
}

void getAccessTokenWithoutRefreshToken(const std::string& filename, const std::string& client_id, const std::string& client_secret) {
    std::string auth_url = "https://accounts.google.com/o/oauth2/v2/auth?"
                          "client_id=" + client_id +
                          "&redirect_uri=" + Config::REDIRECT_URI +
                          "&response_type=code" +
                          "&scope=" + Config::SCOPE +
                          "&access_type=offline" +
                          "&prompt=consent";
    std::cout << "Please open this URL in your browser and authorize the app:\n" << auth_url << std::endl;
    std::cout << "After authorization, copy the authorization code from the redirect URL and paste it here: ";
    std::string auth_code;
    std::getline(std::cin, auth_code);

    std::string token_url = "https://oauth2.googleapis.com/token";
    std::string post_data = "code=" + auth_code +
                           "&client_id=" + client_id +
                           "&client_secret=" + client_secret +
                           "&redirect_uri=" + Config::REDIRECT_URI +
                           "&grant_type=authorization_code";

    std::string token_response = sendRequest(token_url, post_data);

    if (!token_response.empty()) {
        try {
            auto jsonResponse = json::parse(token_response, nullptr, false);
            if (jsonResponse.is_discarded()) {
                std::cerr << "JSON parse error: Invalid JSON response\n";
                LogToFile("JSON parse error: Invalid JSON response");
                return;
            }
            if (jsonResponse.contains("access_token")) {
                std::string access_token = jsonResponse["access_token"].get<std::string>();
                std::string refresh_token = jsonResponse.value("refresh_token", "");
                std::ofstream oss(filename);
                oss << access_token << '\n' << refresh_token;
                oss.close();
                std::cout << "Saved token to " << filename << '\n';
                LogToFile("Saved token to " + filename);
            } else {
                std::cerr << "Error getting token: " << token_response << std::endl;
                LogToFile("Error getting token: " + token_response);
            }
        } catch (const json::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << "\nResponse: " << token_response << std::endl;
            LogToFile("JSON parse error: " + std::string(e.what()) + "\nResponse: " + token_response);
        }
    }
}

void getAccessTokenWithRefreshToken(const std::string& filename, const std::string& client_id, const std::string& client_secret, const std::string& refresh_token) {
    auto refreshAccessToken = [&](const std::string& client_id, const std::string& client_secret, const std::string& refresh_token) -> std::string {
        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Failed to initialize curl\n";
            LogToFile("Failed to initialize curl");
            return "";
        }

        std::string response;
        std::string postData = "client_id=" + client_id +
                              "&client_secret=" + client_secret +
                              "&refresh_token=" + refresh_token +
                              "&grant_type=refresh_token";

        curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_CAINFO, Config::CACERT.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << "\n";
            LogToFile("curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)));
            curl_easy_cleanup(curl);
            return "";
        }

        curl_easy_cleanup(curl);

        try {
            auto jsonResponse = json::parse(response, nullptr, false);
            if (jsonResponse.is_discarded()) {
                std::cerr << "JSON parse error: Invalid JSON response\n";
                LogToFile("JSON parse error: Invalid JSON response");
                return "";
            }
            if (jsonResponse.contains("access_token")) {
                return jsonResponse["access_token"].get<std::string>();
            } else {
                std::cerr << "No access_token in response:\n" << response << "\n";
                LogToFile("No access_token in response:\n" + response);
            }
        } catch (const std::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << "\n";
            LogToFile("JSON parse error: " + std::string(e.what()));
        }
        return "";
    };

    std::string access_token = refreshAccessToken(client_id, client_secret, refresh_token);
    if (!access_token.empty()) {
        std::ofstream oss(filename);
        oss << access_token << '\n' << refresh_token;
        oss.close();
        std::cout << "Saved token to " << filename << '\n';
        LogToFile("Saved token to " + filename);
    }
}

bool getAccessToken(std::string& access_token) {
    std::string client_id, client_secret;

    if (!readCredentials(Config::CREDENTIALS_FILE, client_id, client_secret)) {
        std::cerr << "Exiting due to credentials error." << std::endl;
        LogToFile("Exiting due to credentials error.");
        return false;
    }
    std::cout << "Client ID: " << client_id << std::endl;
    std::cout << "Client Secret: " << client_secret << std::endl;
    LogToFile("Client ID: " + client_id);
    LogToFile("Client Secret: " + client_secret);

    std::string refresh_token;
    if (!readToken(Config::TOKEN_FILE, access_token, refresh_token)) {
        getAccessTokenWithoutRefreshToken(Config::TOKEN_FILE, client_id, client_secret);
        return readToken(Config::TOKEN_FILE, access_token, refresh_token);
    }

    if (!isAccessTokenValid(access_token)) {
        if (refresh_token.empty()) {
            getAccessTokenWithoutRefreshToken(Config::TOKEN_FILE, client_id, client_secret);
        } else {
            getAccessTokenWithRefreshToken(Config::TOKEN_FILE, client_id, client_secret, refresh_token);
        }
        return readToken(Config::TOKEN_FILE, access_token, refresh_token);
    }
    return true;
}