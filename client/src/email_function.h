#pragma once

#include "config.h"
#include <string>

std::string base64_decode_url(const std::string& input);
std::string base64_encode(const std::string& input);
std::string base64_encode_url(const std::string& input);
std::string sendGetRequest(const std::string& url, const std::string& access_token);
bool markAsRead(const std::string& msgId, const std::string& access_token);
void readMail(const std::string& filename, const std::string& access_token);
std::string readFileContent(const std::string& filepath);
bool sendMail(const std::string& access_token, const std::string& to_email, const std::string& filename);