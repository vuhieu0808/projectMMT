#define WIN32_LEAN_AND_MEAN

#include "email_function.h"
#include "email_utils.h"
#include "utils.h"

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string base64_decode_url(const std::string& input) {
    std::string encoded = input;
    std::replace(encoded.begin(), encoded.end(), '-', '+');
    std::replace(encoded.begin(), encoded.end(), '_', '/');
    while (encoded.length() % 4 != 0) {
        encoded += '=';
    }

    BIO* bio, *b64;
    int decodeLen = static_cast<int>(encoded.length() * 3 / 4);
    std::vector<char> buffer(decodeLen);

    bio = BIO_new_mem_buf(encoded.c_str(), -1);
    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);

    int length = BIO_read(bio, buffer.data(), static_cast<int>(buffer.size()));
    BIO_free_all(bio);

    return std::string(buffer.data(), length);
}

std::string base64_encode(const std::string& input) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_write(bio, input.c_str(), input.length());
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string encoded(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return encoded;
}

std::string base64_encode_url(const std::string& input) {
    std::string encoded = base64_encode(input);
    for (char& c : encoded) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    encoded.erase(std::remove(encoded.begin(), encoded.end(), '='), encoded.end());
    return encoded;
}

std::string sendGetRequest(const std::string& url, const std::string& access_token) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;

    if (curl) {
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token).c_str());
        headers = curl_slist_append(headers, "Accept: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_CAINFO, Config::CACERT.c_str());

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            LogToFile("curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)));
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return readBuffer;
}

bool markAsRead(const std::string& msgId, const std::string& access_token) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;
    if (!curl) return false;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string url = "https://gmail.googleapis.com/gmail/v1/users/me/messages/" + msgId + "/modify";
    std::string postData = R"({"removeLabelIds": ["UNREAD"]})";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_CAINFO, Config::CACERT.c_str());

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        LogToFile("curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return http_code == 200;
}

void readMail(const std::string& filename, const std::string& access_token) {
    std::string listUrl = "https://www.googleapis.com/gmail/v1/users/me/messages?q=subject:TeamView-vuhieu+is:unread";
    std::string listResponse = sendGetRequest(listUrl, access_token);

    if (listResponse.empty()) {
        std::cerr << "Cannot fetch email list.\n";
        LogToFile("Cannot fetch email list.");
        return;
    }

    auto jsonList = json::parse(listResponse, nullptr, false);
    if (jsonList.is_discarded() || !jsonList.contains("messages") || jsonList["messages"].empty()) {
        std::cout << "No unread email with subject 'TeamView-vuhieu' found.\n";
        LogToFile("No unread email with subject 'TeamView-vuhieu' found.");
        return;
    }

    std::string msgId = jsonList["messages"][0]["id"];
    std::string msgUrl = "https://www.googleapis.com/gmail/v1/users/me/messages/" + msgId + "?format=full";
    std::string msgResponse = sendGetRequest(msgUrl, access_token);

    auto msgJson = json::parse(msgResponse, nullptr, false);
    if (msgJson.is_discarded()) {
        std::cerr << "Failed to parse message JSON.\n";
        LogToFile("Failed to parse message JSON.");
        return;
    }

    std::string sender;
    if (msgJson.contains("payload") && msgJson["payload"].contains("headers")) {
        for (auto& h : msgJson["payload"]["headers"]) {
            if (h["name"] == "From") {
                std::string fromHeader = h["value"];
                size_t lt = fromHeader.find('<');
                size_t gt = fromHeader.find('>');
                if (lt != std::string::npos && gt != std::string::npos)
                    sender = fromHeader.substr(lt + 1, gt - lt - 1);
                else
                    sender = fromHeader;
                break;
            }
        }
    }

    std::string encodedBody;
    auto payload = msgJson["payload"];
    if (payload.contains("parts")) {
        for (auto& part : payload["parts"]) {
            if (part.contains("mimeType") && part["mimeType"] == "text/plain" && part["body"].contains("data")) {
                encodedBody = part["body"]["data"];
                break;
            }
        }
    } else if (payload["body"].contains("data")) {
        encodedBody = payload["body"]["data"];
    }

    std::string body = base64_decode_url(encodedBody);
    std::istringstream iss(body);
    std::string line1, line2;
    
    std::getline(iss, line1);
    if (!line1.empty()) line1.pop_back();
    
    std::string temp;
    while (std::getline(iss, temp)) {
        if (!temp.empty()) temp.pop_back();
        temp += ' ';
        line2 += temp;
    }

    std::ofstream outFile(filename);
    outFile << line1 << '\n' << line2 << '\n' << sender << '\n';
    outFile.close();
    std::cout << "Email content saved to " << filename << "\n";
    LogToFile("Email content saved to " + filename);

    if (!markAsRead(msgId, access_token)) {
        std::cerr << "Failed to mark email as read.\n";
        LogToFile("Failed to mark email as read.");
    }
}

std::string readFileContent(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        LogToFile("Failed to open file: " + filepath);
        return "";
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

bool sendMail(const std::string& access_token, const std::string& to_email, const std::string& filename) {
    std::string fileContent = readFileContent(filename);
    if (fileContent.empty()) {
        std::cerr << "No content to send for file: " << filename << std::endl;
        LogToFile("No content to send for file: " + filename);
        return false;
    }
    std::string encodedFile = base64_encode(fileContent);

    std::ostringstream raw;
    raw << "From: me\r\n";
    raw << "To: " << to_email << "\r\n";
    raw << "Subject: TeamView-vuhieu Response\r\n";
    raw << "Content-Type: multipart/mixed; boundary=\"boundary\"\r\n\r\n";

    raw << "--boundary\r\n";
    raw << "Content-Type: text/plain; charset=\"UTF-8\"\r\n\r\n";
    raw << "Please see the attached file.\r\n\r\n";

    raw << "--boundary\r\n";
    raw << "Content-Type: application/octet-stream\r\n";
    raw << "Content-Disposition: attachment; filename=\"" << fs::path(filename).filename().string() << "\"\r\n";
    raw << "Content-Transfer-Encoding: base64\r\n\r\n";
    raw << encodedFile << "\r\n";
    raw << "--boundary--";

    std::string raw_email = base64_encode_url(raw.str());

    json payload = {
        {"raw", raw_email}
    };

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize curl\n";
        LogToFile("Failed to initialize curl");
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, "https://gmail.googleapis.com/gmail/v1/users/me/messages/send");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    std::string json_payload = payload.dump();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CAINFO, Config::CACERT.c_str());

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    if (res != CURLE_OK || status != 200) {
        std::cerr << "Failed to send email. HTTP Status: " << status << "\nResponse: " << response << std::endl;
        LogToFile("Failed to send email. HTTP Status: " + std::to_string(status));
        LogToFile("Response: " + response);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    std::cout << "Email sent successfully to " << to_email << std::endl;
    LogToFile("Email sent successfully to " + to_email);
    return true;
}