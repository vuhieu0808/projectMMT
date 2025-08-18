#include "utils.h"

#include <iostream>
#include <algorithm>
#include <fstream>

using namespace std;

std::string convert_backslashes_to_slashes(std::string path) {
    std::replace(path.begin(), path.end(), L'\\', L'/');
    return path;
}

void LogToFile(const string& message, const string& logPath) {
    ofstream logFile(logPath, ios_base::app);
    if (logFile.is_open()) {
        time_t now = time(nullptr);
        char timeStr[26];
        ctime_s(timeStr, sizeof(timeStr), &now);
        logFile << timeStr << ": " << message << endl;
        logFile.close();
    }
}

void CreateCommandFile(const string& command, const string& filePath) {
    ofstream file(filePath);
    if (file.is_open()) {
        file << command << endl;
        file.close();
        cout << "Created client.txt with command: " << command << endl;
        LogToFile("Created client.txt with command: " + command);
    } else {
        cout << "Failed to create client.txt at " << filePath << endl;
        LogToFile("Failed to create client.txt at " + filePath);
    }
}

bool isValidNumber(const string& str) {
    if (str.empty()) return false;
    for (char c : str) {
        if (!isdigit(c)) return false;
    }
    return true;
}

std::string getFileName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
        return path;
    return path.substr(pos + 1);
}