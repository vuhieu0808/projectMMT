#pragma once

#include <bits/stdc++.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <shlwapi.h>
#include <string>
#include <exception>
#include <winsock2.h>

#ifndef UNREFERENCED_PARAMETER 
#define UNREFERENCED_PARAMETER(p) {(p) = (p);}
#endif

// Utility functions
bool WideStringToString(const std::wstring& wstr, std::string& str);
bool StringToWideString(const std::string& str, std::wstring& wstr);

template <class T> void ComSafeRelease(T **ppT);

// Video configuration struct
struct VideoConfig {
    unsigned int width, height;
    unsigned int fps;
    unsigned int bitrate;
    GUID encodingFormat = MFVideoFormat_H264;
};

// Neeko class declaration
class neeko {
private:
    VideoConfig config;
    IMFMediaSource *mediaSource = nullptr;
    IMFAttributes *pAttributes = nullptr;
    IMFActivate **ppDevices = nullptr;
    IMFSourceReader *sourceReader = nullptr;
    IMFMediaType *mediaType = nullptr, *nativeMediaType = nullptr, *sourceReaderOutput = nullptr, *sinkWriterInput = nullptr;
    IMFSinkWriter *sinkWriter = nullptr;
    DWORD streamIndex;
    int allocated = 0;
    HRESULT res;

    void invokeException(std::string functionName, int line, std::string context = "");
    void computeRatio(const unsigned int& width, const unsigned int& height, unsigned int& widthRatio, unsigned int& heightRatio);
    void printMFAttributeInformation(IMFAttributes *attr, const std::string& extraInfo = "");
    void createVideoDeviceSource();
    void encodeRaw();
    void configOutput(const std::wstring& filename);
    void capture(unsigned int millisecond);
    void run(int second);

public:
    neeko() = delete;
    neeko(const VideoConfig& config, int second, const std::wstring& outputFile);
    neeko(const neeko& rhs) = delete;
    ~neeko();
};

// Function to capture video
bool CaptureVideo(SOCKET clientSocket, const std::wstring& outputFile, int durationSeconds = 0);