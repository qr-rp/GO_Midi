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

:: Set wxWidgets path (use backslashes for Windows batch compatibility)
set WX_ROOT=E:\workspace\GO_Midi\wxWidgets-3.3.1

:: Check if wxWidgets is already compiled
if not exist "%WX_ROOT%\lib\gcc_lib\libwxmsw33u.a" (
    echo.
    echo wxWidgets not compiled. Building wxWidgets first...
    echo This may take 10-30 minutes, please wait...
    echo.
    
    :: Create necessary directories first
    if not exist "%WX_ROOT%\lib\gcc_lib" mkdir "%WX_ROOT%\lib\gcc_lib"
    if not exist "%WX_ROOT%\lib\gcc_lib\mswu\wx" mkdir "%WX_ROOT%\lib\gcc_lib\mswu\wx"
    
    :: Copy setup.h if not exists
    if not exist "%WX_ROOT%\lib\gcc_lib\mswu\wx\setup.h" (
        copy "%WX_ROOT%\include\wx\msw\setup.h" "%WX_ROOT%\lib\gcc_lib\mswu\wx\setup.h"
    )
    
    pushd "%WX_ROOT%\build\msw"
    
    mingw32-make -f makefile.gcc SHARED=0 UNICODE=1 BUILD=release -j%NUMBER_OF_PROCESSORS%
    if %errorlevel% neq 0 (
        echo Error: wxWidgets build failed.
        popd
        pause
        exit /b 1
    )
    
    echo.
    echo wxWidgets build successful!
    echo.
    
    popd
) else (
    echo wxWidgets already compiled, skipping...
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
set WX_ROOT_PARAM=-DwxWidgets_ROOT_DIR="%WX_ROOT%"

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
