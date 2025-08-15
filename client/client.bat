@echo off
setlocal enabledelayedexpansion

echo Build and Run Client

set INCLUDE_DIR=include
set LIB_DIR=lib

set SRC_FILES=client.cpp src\network.cpp src\utils.cpp src\email_utils.cpp src\email_function.cpp

:: Build dynamic executable
if exist "%LIB_DIR%\libcurl.dll.a" (
    echo Found libcurl.dll.a - Building dynamic executable...
    g++ -o client %SRC_FILES% ^
        -I%INCLUDE_DIR% -I%INCLUDE_DIR%\curl -I%INCLUDE_DIR%\nlohmann -I%INCLUDE_DIR%\openssl ^
        -L%LIB_DIR% -lcurl -lssl -lcrypto -lws2_32 -lshlwapi -lz -lcrypt32
    if %ERRORLEVEL% NEQ 0 (
        echo Build failed.
        pause
        exit /b %ERRORLEVEL%
    )
    for %%f in (%LIB_DIR%\*.dll) do (
        copy "%%f" .
    )
) else (
    echo ERROR: No libcurl.dll.a found in %LIB_DIR%
    pause
    exit /b 1
)

echo.
echo Run Client:
client.exe
pause
