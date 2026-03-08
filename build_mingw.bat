@echo off
setlocal enabledelayedexpansion

:: Switch to script's directory (supports running from any location)
cd /d "%~dp0"

echo ==========================================
echo Building GO_MIDI with MinGW
echo ==========================================

:: Check for CMake
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: CMake is not found in PATH.
    echo Please install CMake and add it to PATH.
    pause
    exit /b 1
)

:: Check for GCC
where g++ >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: g++ ^(MinGW^) is not found in PATH.
    echo Please install MinGW and add it to PATH.
    pause
    exit /b 1
)

:: Check for wxWidgets source
if not exist "wxWidgets-3.3.1\CMakeLists.txt" (
    echo Error: wxWidgets 3.3.1 source not found.
    echo.
    echo Please download wxWidgets 3.3.1 source and extract to:
    echo   %CD%\wxWidgets-3.3.1\
    echo.
    echo Download URL: https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.1/wxWidgets-3.3.1.tar.bz2
    pause
    exit /b 1
)

:: Detect make command (mingw32-make or make)
set "MAKE_CMD="
where mingw32-make >nul 2>nul
if %errorlevel% equ 0 (
    set "MAKE_CMD=mingw32-make"
) else (
    where make >nul 2>nul
    if %errorlevel% equ 0 (
        set "MAKE_CMD=make"
    )
)
if "%MAKE_CMD%"=="" (
    echo Error: Neither mingw32-make nor make found in PATH.
    pause
    exit /b 1
)
echo Using make command: %MAKE_CMD%

:: Set build directory
set BUILD_DIR=build_mingw

:: Check if reconfiguration is needed
set NEED_CONFIGURE=0
if not exist %BUILD_DIR%\CMakeCache.txt (
    set NEED_CONFIGURE=1
)

:: Create build directory if not exists
if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

:: Configure if needed
if %NEED_CONFIGURE% equ 1 (
    echo.
    echo Configuring with CMake...
    cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
    if %errorlevel% neq 0 (
        echo Error: CMake configuration failed.
        cd ..
        pause
        exit /b 1
    )
) else (
    echo.
    echo Using existing configuration.
)

:: Build
echo.
echo Building... (incremental build, only changed files will be recompiled)
%MAKE_CMD% -j%NUMBER_OF_PROCESSORS%
if %errorlevel% neq 0 (
    echo Error: Build failed.
    cd ..
    pause
    exit /b 1
)

echo.
echo ==========================================
echo Build successful!
echo Executable: %BUILD_DIR%\GO_MIDI!.exe
echo ==========================================
cd ..
pause
