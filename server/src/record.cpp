#include "record.h"
#include "utils.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <thread>

using namespace std;

bool WideStringToString(const std::wstring& wstr, std::string& str) {
    unsigned int len;
    int res = 0, lasterror = 0;
    UNREFERENCED_PARAMETER(lasterror);
    len = WideCharToMultiByte(65001, 0, wstr.c_str(), (int)wstr.length(), nullptr, 0, nullptr, nullptr);
    char *buf = new char[len + 3];
    memset(buf, 0, len + 3);
    res = WideCharToMultiByte(65001, 0, wstr.c_str(), (int)wstr.length(), buf, len, nullptr, nullptr);
    if (res == 0) lasterror = GetLastError();
    if (res) {
        str = std::string(buf, len);
        delete[] buf;
    }
    return (bool)res;
}

bool StringToWideString(const std::string& str, std::wstring& wstr) {
    unsigned int len;
    int res = 0, lasterror = 0;
    UNREFERENCED_PARAMETER(lasterror);
    len = MultiByteToWideChar(65001, 0, str.c_str(), (int)str.length(), nullptr, 0);
    wchar_t *buf = new wchar_t[len + 1];
    memset(buf, 0, len * 2 + 2);
    res = MultiByteToWideChar(65001, 0, str.c_str(), (int)str.length(), buf, len);
    if (res == 0) lasterror = GetLastError();
    if (res) {
        wstr = std::wstring(buf, len);
        delete[] buf;
    }
    return (bool)res;
}

template <class T> void ComSafeRelease(T **ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

void neeko::invokeException(std::string functionName, int line, std::string context) {
    std::string str = "Exception in line " + to_string(line) + ", function " + functionName + ": " + context + "\n";
    char buf[201];
    memset(buf, 0, sizeof buf);
    snprintf(buf, 200, "Last HRESULT code: %X\n", (int)res);
    str += std::string(buf);
    throw std::runtime_error(str);
}

void neeko::computeRatio(const unsigned int& width, const unsigned int& height, unsigned int& widthRatio, unsigned int& heightRatio) {
    unsigned int factor = std::__gcd(width, height);
    widthRatio = width / factor;
    heightRatio = height / factor;
}

void neeko::printMFAttributeInformation(IMFAttributes *attr, const std::string& extraInfo) {
    unsigned int stringLength, symbolicLength;
    wchar_t *wcharName = nullptr, *wcharSymbolicName = nullptr;
    std::wstring wstrName, wstrSymbolic;
    std::string strName = "<null>", strSymbolic = "<null>";

    attr->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &wcharName, &stringLength);
    attr->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &wcharSymbolicName, &symbolicLength);

    if (wcharName) {
        wstrName = std::wstring(wcharName, stringLength);
        WideStringToString(wstrName, strName);
        CoTaskMemFree(wcharName);
    }
    if (wcharSymbolicName) {
        wstrSymbolic = std::wstring(wcharSymbolicName, symbolicLength);
        WideStringToString(wstrSymbolic, strSymbolic);
        CoTaskMemFree(wcharSymbolicName);
    }
    LogToFile("Attribute information:\nName: " + strName + "\nSymbolic link: " + strSymbolic + "\nUser extra info: " + extraInfo + "\n");
    // std::cout << "Attribute information:\nName: " << strName << "\nSymbolic link: " << strSymbolic << "\nUser extra info: " << extraInfo << std::endl;
}

void neeko::createVideoDeviceSource() {
    res = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }
    allocated++;

    res = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }

    unsigned int deviceCount;
    res = MFEnumDeviceSources(pAttributes, &ppDevices, &deviceCount);
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }
    allocated++;

    if (deviceCount == 0) {
        res = E_FAIL;
        invokeException(__func__, __LINE__, "No device found");
    }

    printMFAttributeInformation(ppDevices[0], "Device");

    res = ppDevices[0]->ActivateObject(IID_PPV_ARGS(&mediaSource));
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }
}

void neeko::encodeRaw() {
    res = MFCreateSourceReaderFromMediaSource(mediaSource, pAttributes, &sourceReader);
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }

    sourceReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeMediaType);

    res = MFCreateMediaType(&mediaType);
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }

    res = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    res = mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);

    res = sourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mediaType);
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }

    res = sourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &sourceReaderOutput);
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }
}

void neeko::configOutput(const std::wstring& filename) {
    res = MFCreateSinkWriterFromURL(filename.c_str(), nullptr, nullptr, &sinkWriter);
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }

    res = MFCreateMediaType(&sinkWriterInput);
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }

    unsigned int videoWidth = config.width;
    unsigned int videoHeight = config.height;
    unsigned int sourceWidth, sourceHeight, videoWidthRatio, videoHeightRatio;
    res = MFGetAttributeSize(sourceReaderOutput, MF_MT_FRAME_SIZE, &sourceWidth, &sourceHeight);
    if (videoWidth == 0) videoWidth = sourceWidth;
    if (videoHeight == 0) videoHeight = sourceHeight;
    computeRatio(videoWidth, videoHeight, videoWidthRatio, videoHeightRatio);

    res = sinkWriterInput->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    res = sinkWriterInput->SetGUID(MF_MT_SUBTYPE, config.encodingFormat);
    res = sinkWriterInput->SetUINT32(MF_MT_AVG_BITRATE, config.bitrate);
    res = sinkWriterInput->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    res = MFSetAttributeSize(sinkWriterInput, MF_MT_FRAME_SIZE, videoWidth, videoHeight);
    res = MFSetAttributeRatio(sinkWriterInput, MF_MT_PIXEL_ASPECT_RATIO, videoWidthRatio, videoHeightRatio);
    res = MFSetAttributeRatio(sinkWriterInput, MF_MT_FRAME_RATE, config.fps, 1);

    res = sinkWriter->AddStream(sinkWriterInput, &streamIndex);
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }

    res = sinkWriter->SetInputMediaType(streamIndex, sourceReaderOutput, nullptr);
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }
}

void neeko::capture(unsigned int millisecond) {
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }

    int startTime = GetTickCount();
    bool firstSampleCollected = false;
    long long absoluteTimestamp = 0, timestamp;
    long long sampleDuration = (long long)millisecond * 10000 / config.fps;
    IMFSample* sample = nullptr;
    DWORD streamFlags = 0;
    res = sinkWriter->BeginWriting();
    while (GetTickCount() - startTime < millisecond) {
        res = sourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            nullptr,
            &streamFlags,
            &timestamp,
            &sample
        );

        if (FAILED(res)) {
            LogToFile("Failed reading sample. Break!\n");
            break;
        } else if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
            LogToFile("End of stream. Break!\n");
            break;
        }

        if (sample) {
            if (!firstSampleCollected) {
                firstSampleCollected = true;
                absoluteTimestamp = timestamp;
            }
            long long relativeTimestamp = timestamp - absoluteTimestamp;
            sample->SetSampleTime(relativeTimestamp);
            sample->SetSampleDuration(sampleDuration);
            res = sinkWriter->WriteSample(streamIndex, sample);
            ComSafeRelease(&sample);
            if (FAILED(res)) {
                LogToFile("Writing sample failed. Break!\n");
                break;
            }
        }
    }

    res = sinkWriter->Finalize();
    if (FAILED(res)) {
        invokeException(__func__, __LINE__);
    }
}

void neeko::run(int second) {
    createVideoDeviceSource();
    encodeRaw();
    capture(second * 1000);
}

neeko::neeko(const VideoConfig& config, int second, const std::wstring& outputFile) {
    this->config = config;
    res = MFStartup(MF_VERSION);
    createVideoDeviceSource();
    encodeRaw();
    configOutput(outputFile);
    capture(second * 1000);
}

neeko::~neeko() {
    ComSafeRelease(&mediaSource);
    ComSafeRelease(&pAttributes);
    ComSafeRelease(&sourceReader);
    ComSafeRelease(&mediaType);
    ComSafeRelease(&nativeMediaType);
    ComSafeRelease(&sourceReaderOutput);
    ComSafeRelease(&sinkWriterInput);
    ComSafeRelease(&sinkWriter);

    if (ppDevices && allocated >= 2) {
        unsigned int deviceCount;
        if (SUCCEEDED(MFEnumDeviceSources(pAttributes, &ppDevices, &deviceCount))) {
            for (unsigned int i = 0; i < deviceCount; ++i) {
                ComSafeRelease(&ppDevices[i]);
            }
            CoTaskMemFree(ppDevices);
            ppDevices = nullptr;
        }
    }

    if (SUCCEEDED(res)) {
        res = MFShutdown();
    }
}

bool CaptureVideo(SOCKET clientSocket, const wstring& outputFile, int durationSeconds) {
    // Remove old video file if it exists
    std::remove(wstring_to_string(outputFile).c_str());

    string msg = "Starting video capture to: " + wstring_to_string(outputFile) + "\n";
    send(clientSocket, msg.c_str(), msg.size(), 0);
    LogToFile("Starting video capture to: " + wstring_to_string(outputFile));
    Sleep(100);

    try {
        VideoConfig config;
        config.bitrate = 1000000;
        config.width = 800;
        config.height = 800;
        config.fps = 60;
        config.encodingFormat = MFVideoFormat_H264;

        neeko obj(config, durationSeconds, outputFile);

        // Check if the video file was created successfully
        std::ifstream checkFile(wstring_to_string(outputFile), std::ios::binary | std::ios::ate);
        if (!checkFile.is_open()) {
            msg = "ERROR: Video file not accessible after recording\n";
            send(clientSocket, msg.c_str(), msg.size(), 0);
            LogToFile("ERROR: Video file not accessible after recording");
            return false;
        }

        std::streamsize fileSize = checkFile.tellg();
        checkFile.close();

        if (fileSize <= 0) {
            msg = "ERROR: Video file is empty after recording\n";
            send(clientSocket, msg.c_str(), msg.size(), 0);
            LogToFile("ERROR: Video file is empty after recording");
            return false;
        }

        msg = "Video saved to: " + wstring_to_string(outputFile) + "\n";
        send(clientSocket, msg.c_str(), msg.size(), 0);
        LogToFile("Video saved to: " + wstring_to_string(outputFile));
        return true;

    } catch (const std::exception& e) {
        string msg = "ERROR: Video recording failed - " + string(e.what()) + "\n";
        send(clientSocket, msg.c_str(), msg.size(), 0);
        LogToFile("ERROR: Video recording failed - " + string(e.what()));
        return false;
    }
}