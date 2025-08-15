#pragma once

#include <string>
#include "config.h"

using namespace std;

bool ReadEmailContent(string& ipPort, string& command, string& fromEmail, const string& filePath = Config::EMAIL_CONTENT_PATH);
size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* s);
string sendRequest(const string& url, const string& postData = "");
bool isAccessTokenValid(const std::string& access_token);
bool readCredentials(const std::string& filename, std::string& client_id, std::string& client_secret);
bool readToken(const std::string& filename, std::string& access_token, std::string& refresh_token);
void getAccessTokenWithoutRefreshToken(const std::string& filename, const std::string& client_id, const std::string& client_secret);
void getAccessTokenWithRefreshToken(const std::string& filename, const std::string& client_id, const std::string& client_secret, const std::string& refresh_token);
bool getAccessToken(std::string& access_token);