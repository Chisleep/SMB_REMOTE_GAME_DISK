@echo off
REM ============================================================
REM   RemoteGameHub 开发环境一键部署脚本 (PowerShell 5 / Win10+)
REM   作用:
REM     1) 下载安装 WinFsp (虚拟盘驱动) + WinFsp SDK
REM     2) 下载 SQLite 预编译 x64 二进制和头文件
REM     3) 自动检测 Visual Studio 2022 / CMake / dotnet
REM     4) 生成 C++ 构建目录 / GUI 构建目录
REM     5) 一键编译: BuildAll.bat
REM
REM   用法:
REM     右键 - 以管理员身份运行
REM     或:  powershell -Command "Set-ExecutionPolicy Bypass -Scope Process" ; .\DevEnvSetup.bat
REM ============================================================

setlocal EnableDelayedExpansion
chcp 65001 >nul

cd /d "%~dp0"
set PROJECT_DIR=%~dp0
set DEPS_DIR=%PROJECT_DIR%ThirdParty
set LOG_FILE=%PROJECT_DIR%setup.log

echo.
echo ============================================
echo   RemoteGameHub 开发环境部署
echo ============================================
echo 项目目录: %PROJECT_DIR%
echo 依赖目录: %DEPS_DIR%
echo 日志文件: %LOG_FILE%
echo ============================================
echo.

if not exist "%DEPS_DIR%" mkdir "%DEPS_DIR%"
echo [时间] %date% %time% >"%LOG_FILE%"

REM ============================================================
REM  1. 检测环境
REM ============================================================
echo.
echo [1/6] 检测开发工具...

where powershell >nul 2>&1 || (echo   [X] 未检测到 PowerShell& exit /b 1)
echo   [√] PowerShell

where cmake >nul 2>&1
if %errorlevel%==0 (
    for /f "tokens=*" %%i in ('cmake --version 2^>^&1 ^| findstr /i "version"') do (echo   [√] CMake  %%i)
) else (
    echo   [!] 未检测到 CMake。请用以下命令安装, 或下载后加到 PATH:
    echo        winget install Kitware.CMake -e
    echo        或访问 https://cmake.org/download/ 下载 Windows x64 MSI (勾选 Add to PATH)
    echo.
)

where dotnet >nul 2>&1
if %errorlevel%==0 (
    for /f "tokens=*" %%i in ('dotnet --version 2^>^&1') do (echo   [√] dotnet  %%i)
) else (
    echo   [!] 未检测到 dotnet (需要 .NET 8 SDK)
    echo        请运行: winget install Microsoft.DotNet.SDK.8 -e
    echo        或访问 https://dotnet.microsoft.com/download
)

where git >nul 2>&1
if %errorlevel%==0 (
    echo   [√] git
) else (
    echo   [!] 未检测到 git (可选, 用于源码管理)
    echo        winget install Git.Git -e
)

REM ============================================================
REM  2. WinFsp 运行时 + SDK 安装
REM ============================================================
echo.
echo [2/6] WinFsp 运行时 + SDK...
set WINFSP_VERSION=2.0.2405
set WINFSP_URL=https://github.com/winfsp/winfsp/releases/download/v%WINFSP_VERSION%
set WINFSP_RUNTIME=%DEPS_DIR%\winfsp-%WINFSP_VERSION%.msi
set WINFSP_SDK=%DEPS_DIR%\WinFsp-%WINFSP_VERSION%-tools.zip
set WINFSP_INSTALL_DIR=C:\Program Files (x86)\WinFsp

if exist "%WINFSP_INSTALL_DIR%\inc\winfsp\winfsp.h" (
    echo   [√] WinFsp SDK 已安装 (%WINFSP_INSTALL_DIR%)
) else (
    echo   正在下载 WinFsp 运行时 ...
    if not exist "%WINFSP_RUNTIME%" (
        powershell -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri '%WINFSP_URL%/winfsp-%WINFSP_VERSION%.msi' -OutFile '%WINFSP_RUNTIME%' -UseBasicParsing"
        if not exist "%WINFSP_RUNTIME%" (
            echo   [X] WinFsp 运行时下载失败
            echo        请手工下载: %WINFSP_URL%/winfsp-%WINFSP_VERSION%.msi
        )
    )
    if exist "%WINFSP_RUNTIME%" (
        echo   正在以静默方式安装 WinFsp 运行时 (需管理员权限)...
        msiexec /i "%WINFSP_RUNTIME%" /quiet /norestart ADDLOCAL=Driver,Service
        if %errorlevel%==3010 (echo   [√] WinFsp 运行时已安装, 重启生效)
        if %errorlevel%==0 (echo   [√] WinFsp 运行时已安装)
    )
)

REM ============================================================
REM  3. SQLite 预编译二进制 + 头文件
REM ============================================================
echo.
echo [3/6] SQLite 预编译二进制 ...
set SQLITE_DIR=%DEPS_DIR%\sqlite
set SQLITE_DLL_URL=https://www.sqlite.org/2024/sqlite-dll-win-x64-3450100.zip
set SQLITE_DLL=%DEPS_DIR%\sqlite-dll.zip
set SQLITE_SRC_URL=https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip
set SQLITE_SRC=%DEPS_DIR%\sqlite-src.zip

if not exist "%SQLITE_DIR%" mkdir "%SQLITE_DIR%"

if not exist "%SQLITE_DIR%\sqlite3.h" (
    echo   正在下载 SQLite amalgamation 源码...
    if not exist "%SQLITE_SRC%" (
        powershell -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri '%SQLITE_SRC_URL%' -OutFile '%SQLITE_SRC%' -UseBasicParsing"
    )
    if exist "%SQLITE_SRC%" (
        powershell -Command "Expand-Archive -Force -LiteralPath '%SQLITE_SRC%' -DestinationPath '%DEPS_DIR%'"
        copy /y "%DEPS_DIR%\sqlite-amalgamation-3450100\sqlite3.h" "%SQLITE_DIR%\" >nul
        copy /y "%DEPS_DIR%\sqlite-amalgamation-3450100\sqlite3.c" "%SQLITE_DIR%\" >nul
        echo   [√] SQLite 源码已解压到 %SQLITE_DIR%
    ) else (
        echo   [!] SQLite 源码下载失败, 请手动从 https://www.sqlite.org/download.html 下载
    )
) else (
    echo   [√] SQLite 源码已存在
)

if not exist "%SQLITE_DIR%\sqlite3.dll" (
    echo   正在下载 SQLite 预编译 DLL ...
    if not exist "%SQLITE_DLL%" (
        powershell -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri '%SQLITE_DLL_URL%' -OutFile '%SQLITE_DLL%' -UseBasicParsing"
    )
    if exist "%SQLITE_DLL%" (
        powershell -Command "Expand-Archive -Force -LiteralPath '%SQLITE_DLL%' -DestinationPath '%SQLITE_DIR%'"
        echo   [√] SQLite DLL 已解压到 %SQLITE_DIR%
    ) else (
        echo   [!] SQLite DLL 下载失败, 请手动下载
    )
) else (
    echo   [√] SQLite 二进制已存在
)

REM ============================================================
REM  4. 生成本地 lib 文件 (sqlite3.dll -> sqlite3.lib)
REM ============================================================
echo.
echo [4/6] 生成 sqlite3.lib 导入库...
where lib >nul 2>&1
if %errorlevel%==0 (
    pushd "%SQLITE_DIR%"
    if exist sqlite3.def del /q sqlite3.def
    echo EXPORTS > sqlite3.def
    echo sqlite3_close >> sqlite3.def
    echo sqlite3_exec >> sqlite3.def
    echo sqlite3_finalize >> sqlite3.def
    echo sqlite3_libversion >> sqlite3.def
    echo sqlite3_open_v2 >> sqlite3.def
    echo sqlite3_prepare_v2 >> sqlite3.def
    echo sqlite3_step >> sqlite3.def
    echo sqlite3_column_text >> sqlite3.def
    echo sqlite3_column_int >> sqlite3.def
    echo sqlite3_column_int64 >> sqlite3.def
    echo sqlite3_bind_text >> sqlite3.def
    echo sqlite3_bind_int >> sqlite3.def
    echo sqlite3_bind_int64 >> sqlite3.def
    echo sqlite3_bind_double >> sqlite3.def
    echo sqlite3_bind_null >> sqlite3.def
    echo sqlite3_errmsg >> sqlite3.def
    echo sqlite3_changes >> sqlite3.def
    echo sqlite3_last_insert_rowid >> sqlite3.def
    echo sqlite3_column_count >> sqlite3.def
    echo sqlite3_column_name >> sqlite3.def
    echo sqlite3_free >> sqlite3.def
    if exist sqlite3.dll (
        lib /def:sqlite3.def /machine:X64 /out:sqlite3.lib >nul 2>&1
        if exist sqlite3.lib (echo   [√] sqlite3.lib 生成成功) else (echo   [!] sqlite3.lib 生成失败, 稍后重试)
    ) else (
        echo   [!] sqlite3.dll 不存在, 无法生成 .lib
    )
    popd
) else (
    echo   [!] 未检测到 MSVC lib.exe, 请先运行 "Developer Command Prompt for VS 2022"
)

REM ============================================================
REM  5. 创建 .env 配置文件 (路径重写)
REM ============================================================
echo.
echo [5/6] 生成本地路径配置文件...
(
    echo REM ===============================================
    echo REM  RemoteGameHub 本地路径配置
    echo REM  由 DevEnvSetup.bat 自动生成
    echo REM ===============================================
    echo set WINFSP_DIR=%WINFSP_INSTALL_DIR%
    echo set SQLITE3_DIR=%SQLITE_DIR%
    echo set PROJECT_DIR=%PROJECT_DIR%
) > "%PROJECT_DIR%local_paths.bat"
echo   [√] 已生成 local_paths.bat

REM ============================================================
REM  6. 编译 C++ 核心 + WPF GUI
REM ============================================================
echo.
echo [6/6] 编译项目...

set BUILD_DIR=%PROJECT_DIR%build
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

where cmake >nul 2>&1
if %errorlevel%==0 (
    pushd "%BUILD_DIR%"
    echo   正在运行 CMake 配置 (Release x64)...
    cmake -G "Visual Studio 17 2022" -A x64 -DWINFSP_DIR="%WINFSP_INSTALL_DIR%" -DSQLITE3_DIR="%SQLITE_DIR%" "%PROJECT_DIR%"
    if %errorlevel%==0 (
        echo   [√] CMake 配置成功
        echo   正在构建 Release 版本...
        cmake --build . --config Release -j
        if %errorlevel%==0 (
            echo   [√] C++ 核心构建成功
        ) else (
            echo   [!] C++ 核心构建失败, 请检查日志
        )
    ) else (
        echo   [!] CMake 配置失败
    )
    popd
) else (
    echo   [!] 未检测到 CMake, 跳过 C++ 构建
)

where dotnet >nul 2>&1
if %errorlevel%==0 (
    echo   正在构建 WPF GUI...
    pushd "%PROJECT_DIR%GUI\RemoteGameHub.UI"
    dotnet build -c Release -nologo -v quiet
    if %errorlevel%==0 (
        echo   [√] WPF GUI 构建成功
    ) else (
        echo   [!] WPF GUI 构建失败
    )
    popd
) else (
    echo   [!] 未检测到 dotnet, 跳过 GUI 构建
)

echo.
echo ============================================
echo   部署完成
echo ============================================
echo.
echo 已生成的文件:
echo   %PROJECT_DIR%local_paths.bat
echo   %PROJECT_DIR%build\Release\*.exe (如构建成功)
echo   %PROJECT_DIR%GUI\RemoteGameHub.UI\bin\Release\net8.0-windows\RemoteGameHub.UI.exe
echo.
echo 下一步:
echo   1) 重启电脑使 WinFsp 驱动生效
echo   2) 打开 CMD (或"Developer Command Prompt for VS 2022"), 进入 %PROJECT_DIR%
echo   3) 运行:  cd build && Release\rgh_mount.exe 192.168.5.103 Games 游戏目录名 G 用户名 密码
echo   4) 打开资源管理器 G:\, 双击 game.exe 测试
echo ============================================
echo.

endlocal
