@echo off
echo Build and Run Client
g++ -o client client.cpp src\network.cpp src\utils.cpp src\email_utils.cpp  src\email_function.cpp -lws2_32 -lcurl -lssl -lcrypto -lshlwapi

if %ERRORLEVEL% NEQ 0 (
    echo Biên dịch thất bại.
    pause
    exit /b %ERRORLEVEL%
)

echo Run Client:
client.exe
pause
