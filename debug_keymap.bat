@echo off
setlocal enabledelayedexpansion

:: 设置日志文件
set "LOG_FILE=debug_logs.txt"

:: 设置测试文件路径
if "%~1"=="" (
    set "TEST_FILE=keymaps\燕云十六声默认键位.txt"
) else (
    set "TEST_FILE=%~1"
)

:: 设置 UTF-8 代码页
chcp 65001 >nul 2>&1

echo.
echo ================================================================
echo   GO_Midi 键位映射加载诊断工具
echo ================================================================
echo.
echo 正在运行诊断，请稍候...
echo.

:: 调用诊断主程序，输出到日志文件
call :run_diagnostic > "%LOG_FILE%" 2>&1

echo.
echo ================================================================
echo   诊断完成!
echo ================================================================
echo.
echo 日志文件已生成: %LOG_FILE%
echo.
echo 请将 %LOG_FILE% 文件发送给开发者进行问题分析
echo.
echo 日志文件位置: %CD%\%LOG_FILE%
echo.

:: 自动打开日志文件
notepad "%LOG_FILE%" 2>nul

pause
exit /b 0

:run_diagnostic
:: ================================================================
:: 诊断主程序 - 所有输出重定向到日志文件
:: ================================================================

echo ================================================================
echo   GO_Midi 键位映射加载诊断日志
echo ================================================================
echo.
echo 生成时间: %DATE% %TIME%
echo.

echo ================================================================
echo   系统环境信息
echo ================================================================
echo.

echo [系统] Windows 版本
echo --------------------------------------------------------------
ver
echo.

echo [系统] 系统信息 (详细)
echo --------------------------------------------------------------
systeminfo | findstr /i "OS 名称 OS 版本 系统类型 区域" 2>nul
echo.

echo [系统] Windows 版本详情 (注册表)
echo --------------------------------------------------------------
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v ProductName 2^>nul') do echo 产品名称: %%b
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v EditionID 2^>nul') do echo 版本类型: %%b
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v CurrentBuild 2^>nul') do echo 内部版本号: %%b
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v DisplayVersion 2^>nul') do echo 显示版本: %%b
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v CurrentMajorVersionNumber 2^>nul') do echo 主版本号: %%b
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v CurrentMinorVersionNumber 2^>nul') do echo 次版本号: %%b
echo.

echo [系统] Windows 版本类型 (专业版/家庭版/企业版)
echo --------------------------------------------------------------
powershell -Command "try { $edition = Get-WindowsEdition -Online; Write-Host '版本类型:' $edition.Edition } catch { Write-Host '无法获取版本类型 (需要管理员权限)' }" 2>nul
powershell -Command "(Get-CimInstance -ClassName Win32_OperatingSystem).Caption" 2>nul
echo.

echo [系统] 默认代码页 (ANSI/OEM)
echo --------------------------------------------------------------
powershell -Command "Write-Host 'ANSI 代码页 (系统默认):' ([System.Text.Encoding]::Default.CodePage)" 2>nul
powershell -Command "Write-Host 'ANSI 编码名称:' ([System.Text.Encoding]::Default.EncodingName)" 2>nul
powershell -Command "Write-Host 'OEM 代码页 (控制台):' [Console]::OutputEncoding.CodePage" 2>nul
echo.

echo [系统] 区域设置
echo --------------------------------------------------------------
reg query "HKCU\Control Panel\International" /v LocaleName 2>nul | findstr "LocaleName"
reg query "HKCU\Control Panel\International" /v Locale 2>nul | findstr "Locale"
echo 用户区域设置 (Win32 API):
powershell -Command "[System.Globalization.CultureInfo]::CurrentUICulture.Name" 2>nul
echo 系统区域设置 (Win32 API):
powershell -Command "[System.Globalization.CultureInfo]::InstalledUICulture.Name" 2>nul
echo.

echo [系统] 语言和时区
echo --------------------------------------------------------------
powershell -Command "Write-Host '系统语言:' (Get-WinSystemLocale).Name" 2>nul
powershell -Command "Write-Host '用户语言:' (Get-Culture).Name" 2>nul
powershell -Command "Write-Host '时区:' (Get-TimeZone).Id" 2>nul
echo.

echo [系统] .NET / PowerShell 版本
echo --------------------------------------------------------------
powershell -Command "Write-Host 'PowerShell 版本:' $PSVersionTable.PSVersion" 2>nul
powershell -Command "Write-Host '.NET CLR 版本:' $PSVersionTable.CLRVersion" 2>nul
echo.

echo [系统] 用户权限
echo --------------------------------------------------------------
net session >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo 当前用户: 管理员权限
) else (
    echo 当前用户: 普通用户权限
)
whoami
echo.

echo [系统] 环境变量 (相关)
echo --------------------------------------------------------------
echo PATH (前 100 字符): %PATH:~0,100%...
echo TEMP: %TEMP%
echo USERPROFILE: %USERPROFILE%
echo.

echo ================================================================
echo   文件诊断开始
echo ================================================================
echo.

echo [测试文件]
if "%~1"=="" (
    echo 使用默认测试文件: keymaps\燕云十六声默认键位.txt
) else (
    echo 使用用户指定的文件: %~1
)
echo.

echo [步骤 1] 检查当前目录
echo --------------------------------------------------------------
cd
echo 当前目录: %CD%
echo.

echo [步骤 2] 检查测试文件是否存在
echo --------------------------------------------------------------
if exist "%TEST_FILE%" (
    echo [OK] 文件存在: %TEST_FILE%
) else (
    echo [ERROR] 文件不存在: %TEST_FILE%
    echo 请确保在 GO_Midi 目录下运行此脚本
    goto :eof
)
echo.

echo [步骤 3] 检查文件大小
echo --------------------------------------------------------------
for %%F in ("%TEST_FILE%") do (
    set "SIZE=%%~zF"
    echo 文件大小: !SIZE! 字节
    if !SIZE! EQU 0 (
        echo [ERROR] 文件为空
    ) else (
        echo [OK] 文件不为空
    )
)
echo.

echo [步骤 4] 检查文件 BOM (前 3 字节)
echo --------------------------------------------------------------
certutil -f -encodehex "%TEST_FILE%" "%TEMP%\bom_check.txt" 4 >nul 2>&1
set /p FIRST_LINE=<"%TEMP%\bom_check.txt"
echo 文件头 (Hex): %FIRST_LINE%

for /f "skip=1 tokens=1-3 delims= " %%a in ('certutil -f -encodehex "%TEST_FILE%" "%TEMP%\bom_hex.txt" 16 ^| findstr /r "^[0-9a-f]"') do (
    set "BYTE1=%%a"
    set "BYTE2=%%b"
    set "BYTE3=%%c"
    goto :bom_check_done
)
:bom_check_done
echo 前 3 字节: [!BYTE1!] [!BYTE2!] [!BYTE3!]
if "!BYTE1!!BYTE2!!BYTE3!"=="efbbbf" (
    echo [INFO] 检测到 UTF-8 BOM
) else (
    echo [INFO] 无 UTF-8 BOM (正常，代码支持无 BOM 的 UTF-8)
)
echo.

echo [步骤 5] 检查路径编码问题
echo --------------------------------------------------------------
echo 测试文件路径: %TEST_FILE%
echo 路径包含中文: 是 (燕云十六声)
echo.

echo [步骤 6] 测试文件读取 (type 命令)
echo --------------------------------------------------------------
type "%TEST_FILE%" >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] type 命令读取成功
) else (
    echo [ERROR] type 命令读取失败，错误码: %ERRORLEVEL%
)
echo.

echo [步骤 7] 检查文件内容编码 (查找 UTF-8 特征)
echo --------------------------------------------------------------
findstr /r "音符" "%TEST_FILE%" >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] 文件包含中文内容 "音符"
) else (
    echo [WARN] 未找到 "音符"，可能是编码问题或文件内容不同
)
echo.

echo [步骤 8] 统计有效映射行数
echo --------------------------------------------------------------
findstr /r /c:"音符 [0-9][0-9]* (" "%TEST_FILE%" >"%TEMP%\valid_lines.txt" 2>nul
for /f %%i in ('type "%TEMP%\valid_lines.txt" ^| find /c /v ""') do set VALID_COUNT=%%i
echo 有效映射行数: %VALID_COUNT%
if %VALID_COUNT% GEQ 1 (
    echo [OK] 找到有效映射
) else (
    echo [WARN] 未找到有效映射行
)
echo.

echo [步骤 9] 模拟 C++ 文件打开测试 (关键)
echo --------------------------------------------------------------
echo 测试窄字符路径打开 (C++ 回退方案)...
powershell -Command "try { $f = [System.IO.File]::OpenRead('%TEST_FILE%'); $f.Close(); Write-Host '[OK] 窄字符路径打开成功' } catch { Write-Host '[ERROR] 窄字符路径打开失败:' $_.Exception.Message }" 2>nul

echo 测试宽字符路径打开 (C++ 主要方案)...
powershell -Command "try { $path = [System.IO.Path]::GetFullPath('%TEST_FILE%'); $f = [System.IO.File]::OpenRead($path); $f.Close(); Write-Host '[OK] 宽字符路径打开成功' } catch { Write-Host '[ERROR] 宽字符路径打开失败:' $_.Exception.Message }" 2>nul
echo.

echo [步骤 10] 检查完整绝对路径
echo --------------------------------------------------------------
for %%i in ("%TEST_FILE%") do set "FULL_PATH=%%~fi"
echo 完整路径: %FULL_PATH%
echo.

echo [步骤 11] 检查路径中的特殊字符
echo --------------------------------------------------------------
echo %FULL_PATH% | find " " >nul
if %ERRORLEVEL% EQU 0 (
    echo [WARN] 路径包含空格，可能影响某些操作
) else (
    echo [OK] 路径不包含空格
)
echo %FULL_PATH% | findstr /r "[^\x00-\x7F]" >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [INFO] 路径包含非 ASCII 字符 (中文等)
    echo 这可能是导致 MultiByteToWideChar 失败的原因
) else (
    echo [OK] 路径为纯 ASCII
)
echo.

echo [步骤 12] 检查控制台代码页 (运行时)
echo --------------------------------------------------------------
for /f "tokens=2 delims=:" %%i in ('chcp') do set CP=%%i
echo 当前控制台代码页: %CP%
if "%CP%"==" 65001" (
    echo [OK] 控制台已设置为 UTF-8
) else if "%CP%"==" 936" (
    echo [INFO] 控制台为 GBK 编码 (中文 Windows 默认)
) else (
    echo [WARN] 非标准代码页，可能影响中文显示
)
echo.

echo [步骤 13] 显示文件前 10 行内容
echo --------------------------------------------------------------
powershell -Command "Get-Content '%TEST_FILE%' -Encoding UTF8 -TotalCount 10 | ForEach-Object { $_ }" 2>nul
echo.

echo ================================================================
echo   诊断完成 - 问题排查建议
echo ================================================================
echo.

echo [常见问题及解决方案]
echo.
echo 1. 文件打开失败 (步骤 6/9 ERROR)
echo    - 检查文件是否被其他程序占用
echo    - 检查文件路径是否包含特殊字符
echo    - 尝试将文件移动到纯英文路径
echo.
echo 2. 编码问题 (步骤 7 WARN)
echo    - 确保文件保存为 UTF-8 编码
echo    - 使用记事本打开文件，另存为时选择 UTF-8 编码
echo.
echo 3. 中文路径问题 (步骤 11 显示非 ASCII)
echo    - 这是已知问题，Windows API 对中文路径支持不完善
echo    - 临时解决方案：将 keymaps 文件夹移动到英文路径
echo.
echo 4. 代码页问题 (系统信息显示非 65001/936)
echo    - 在程序启动前执行: chcp 65001
echo    - 或修改系统区域设置
echo.

echo ================================================================
echo   日志结束
echo ================================================================
echo.
echo 请将此日志文件发送给开发者进行问题分析
echo.

:: 清理临时文件
del "%TEMP%\bom_check.txt" 2>nul
del "%TEMP%\bom_hex.txt" 2>nul
del "%TEMP%\valid_lines.txt" 2>nul

exit /b 0