@echo off
setlocal

echo ==========================================
echo Building wx_GO_MIDI_CPP with MinGW
echo ==========================================

:: Check for CMake
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: CMake is not found in PATH.
    pause
    exit /b 1
)

:: Check for GCC
where g++ >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: g++ ^(MinGW^) is not found in PATH.
    pause
    exit /b 1
)

:: Set build directory
set BUILD_DIR=build_mingw
if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

:: Tip for user
echo.
echo NOTE: If wxWidgets is not found, you may need to edit this script
echo and set wxWidgets_ROOT_DIR in the cmake command, or set the environment variable.
echo.

:: Configuration
:: Uncomment and set the following line if CMake cannot find wxWidgets:
set WX_ROOT_PARAM=-DwxWidgets_ROOT_DIR="e:/workspace/GO_Midi/wxWidgets-3.3.1"

echo Generating Makefiles...
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release %WX_ROOT_PARAM% ..
if %errorlevel% neq 0 (
    echo.
    echo Error: CMake generation failed.
    echo.
    echo Common solution:
    echo 1. Ensure wxWidgets is extracted and compiled with MinGW.
    echo 2. Set wxWidgets_ROOT_DIR environment variable or edit this script.
    echo.
    cd ..
    pause
    exit /b 1
)

echo.
echo Building...
mingw32-make -j%NUMBER_OF_PROCESSORS%
if %errorlevel% neq 0 (
    echo Error: Build failed.
    cd ..
    pause
    exit /b 1
)

echo.
echo Build successful! Executable is in %BUILD_DIR%
cd ..
pause
