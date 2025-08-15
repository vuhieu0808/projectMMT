#pragma once

#include <string>
#include "config.h"

using namespace std;

std::string convert_backslashes_to_slashes(std::string path);
void LogToFile(const string& message, const string& logPath = Config::LOG_PATH);
void CreateCommandFile(const string& command, const string& filePath = Config::COMMAND_FILE_PATH);
bool isValidNumber(const string& str);
std::string getFileName(const std::string& path);