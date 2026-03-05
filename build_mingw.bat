@echo off
setlocal

echo ==========================================
echo Building GO_MIDI with MinGW
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

:: Check for wxWidgets source
if not exist "wxWidgets-3.3.1\CMakeLists.txt" (
    echo Error: wxWidgets source not found.
    echo Please ensure wxWidgets-3.3.1 directory exists with source code.
    pause
    exit /b 1
)

:: Set build directory
set BUILD_DIR=build_mingw
if exist %BUILD_DIR% rd /s /q %BUILD_DIR%
mkdir %BUILD_DIR%
cd %BUILD_DIR%

echo.
echo Configuring with CMake...
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
if %errorlevel% neq 0 (
    echo Error: CMake configuration failed.
    cd ..
    pause
    exit /b 1
)

echo.
echo Building... (this may take several minutes on first build)
mingw32-make -j%NUMBER_OF_PROCESSORS%
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