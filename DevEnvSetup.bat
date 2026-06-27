@echo off
REM ============================================================
REM   RemoteGameHub 开发环境一键部署脚本 (v2 - 修复括号解析问题
REM   使用: 以管理员身份运行 CMD 或 PowerShell，然后: .\DevEnvSetup.bat
REM ============================================================

setlocal enabledelayedexpansion
chcp 65001 >nul

cd /d "%~dp0"
set "PROJECT_DIR=%~dp0"
set "DEPS_DIR=%~dp0ThirdParty"
set "LOG_FILE=%~dp0setup.log"

REM ===== 写日志的单行
echo ============ RemoteGameHub 部署开始 [%date% %time% > "%LOG_FILE%"

echo.
echo ============================================================
echo   RemoteGameHub 开发环境部署
echo ============================================================
echo   项目目录: %PROJECT_DIR%
echo   依赖目录: %DEPS_DIR%
echo   日志: %LOG_FILE%
echo ============================================================
echo.

if not exist "%DEPS_DIR%" mkdir "%DEPS_DIR%"

REM ============================================================
REM  [1/5] 检查开发工具
REM ============================================================
echo [1/5] 检查开发工具...

set "NEED_CMAKE=0
set "NEED_DOTNET=0
set "NEED_VS=0

where powershell >nul 2>&1
if %errorlevel%==0 (
    echo   [√] PowerShell
) else (
    echo   [!] 未检测到 PowerShell
)

where cmake >nul 2>&1
if %errorlevel%==0 (
    for /f "tokens=* delims=" %%i in ('cmake --version 2^>^&1 ^| findstr /i "version"') do echo   [√] %%i
) else (
    echo   [!] 未检测到 CMake。请安装:
    echo        winget install Kitware.CMake -e --accept-source-agreements --accept-package-agreements
    echo        或访问 https://cmake.org/download/ 下载 Windows x64 MSI（安装时勾选 Add to PATH）
    set NEED_CMAKE=1
)

where dotnet >nul 2>&1
if %errorlevel%==0 (
    for /f "tokens=* delims=" %%i in ('dotnet --version 2^>^&1') do echo   [√] dotnet %%i
    echo        （只要 >= 8.x 即可)
    set NEED_DOTNET=0
) else (
    echo   [!] 未检测到 dotnet。请安装:
    echo        winget install Microsoft.DotNet.SDK.8 -e
    set NEED_DOTNET=1
)

where git >nul 2>&1
if %errorlevel%==0 (
    echo   [√] git
) else (
    echo   [!] 未检测到 git（可选，不影响编译）
)

REM 检查 VS 2022 C++ 工具链 (cl.exe)
where cl >nul 2>&1
if %errorlevel%==0 (
    echo   [√] MSVC cl.exe（已在 PATH 中）
) else (
    echo   [!] 未检测到 MSVC cl.exe（需要 VS 2022）
    echo        请安装 Visual Studio 2022 或 Build Tools for VS 2022，勾选"使用 C++ 的桌面开发"
    echo        winget install Microsoft.VisualStudio.2022.Community
    set NEED_VS=1
)

echo.
if %NEED_CMAKE%%NEED_DOTNET%%NEED_VS% neq 000 (
    echo   [!] 检测到缺失工具，请先安装上述软件，然后重新运行本脚本
    echo.
    choice /c YN /m "是否继续（跳过工具安装？（Y/N"
    if errorlevel 2 goto :eof
) else (
    echo   [√] 主要开发工具都已就绪
)

REM ============================================================
REM  [2/5] WinFsp 运行时 + SDK
REM ============================================================
echo.
echo [2/5] WinFsp 运行时与 SDK

set "WINFSP_OK=0
set "WINFSP_DIR="

REM 优先检测标准安装路径
if exist "C:\Program Files (x86)\WinFsp\bin\winfsp-msil.dll" (
    set "WINFSP_DIR=C:\Program Files (x86)\WinFsp"
    echo   [√] WinFsp SDK 已检测到
    set WINFSP_OK=1
) else if exist "C:\Program Files\WinFsp\bin\winfsp-msil.dll" (
    set "WINFSP_DIR=C:\Program Files\WinFsp"
    echo   [√] WinFsp SDK 已检测到
    set WINFSP_OK=1
)

if %WINFSP_OK%==0 (
    echo   未检测到 WinFsp，正在下载并安装 ...
    powershell -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri 'https://github.com/winfsp/winfsp/releases/download/v2.0.2405/winfsp-2.0.2405.msi' -OutFile '%DEPS_DIR%\winfsp-2.0.2405.msi' -UseBasicParsing 2>nul
    if exist "%DEPS_DIR%\winfsp-2.0.2405.msi" (
        echo   下载完成，正在以静默方式安装...
        msiexec /i "%DEPS_DIR%\winfsp-2.0.2405.msi /quiet /norestart
        if errorlevel 3010 (
            echo   [√] WinFsp 安装完成
            set WINFSP_OK=1
        ) else (
            echo   [!] WinFsp 安装失败，请手动下载安装:
            echo        https://winfsp.dev/rel/
            echo        或使用 Chocolatey 安装:  choco install winfsp
        )
    ) else (
        echo   [!] WinFsp 下载失败，请手动下载安装
        echo        https://winfsp.dev/rel/
    )
)

REM ============================================================
REM  [3/5] SQLite amalgamation + 编译成静态库
REM ============================================================
echo.
echo [3/5] SQLite amalgamation

set "SQLITE_DIR=%DEPS_DIR%\sqlite"
set "SQLITE_OK=0

if not exist "%SQLITE_DIR%" mkdir "%SQLITE_DIR%"

if exist "%SQLITE_DIR%\sqlite3.h" if exist "%SQLITE_DIR%\sqlite3.c" (
    echo   [√] SQLite 源码已存在
    set SQLITE_OK=1
)

if %SQLITE_OK%==0 (
    echo   正在下载 SQLite amalgamation...
    powershell -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri 'https://sqlite.org/2024/sqlite-amalgamation-3450000.zip' -OutFile '%DEPS_DIR%\sqlite-amalgamation-3450000.zip' -UseBasicParsing 2>nul
    if exist "%DEPS_DIR%\sqlite-amalgamation-3450000.zip" (
        powershell -Command "Expand-Archive -Force -LiteralPath '%DEPS_DIR%\sqlite-amalgamation-3450000.zip' -DestinationPath '%DEPS_DIR%" >nul 2>nul
        REM 从解压的子目录拷贝到 SQLITE_DIR
        if exist "%DEPS_DIR%\sqlite-amalgamation-3450000\sqlite3.h" (
            copy /y "%DEPS_DIR%\sqlite-amalgamation-3450000\*.h" "%SQLITE_DIR%\" >nul
            copy /y "%DEPS_DIR%\sqlite-amalgamation-3450000\*.c" "%SQLITE_DIR%\" >nul
            echo   [√] SQLite 解压完成
            set SQLITE_OK=1
        )
    )
)

if %SQLITE_OK%==0 (
    echo   [!] SQLite 自动下载失败，请手动获取:  https://sqlite.org/download.html
)

REM 检查是否有 cl.exe 来编译静态库
if %SQLITE_OK%==1 (
    where cl >nul 2>&1
    if %errorlevel%==0 (
        echo   正在把 amalgamation 编译为 sqlite3.lib ...
        pushd "%SQLITE_DIR%"
        cl /c /O2 /GL /MT /W4 /DNDEBUG /DSQLITE_THREADSAFE=1 /I "%SQLITE_DIR% "%SQLITE_DIR%\sqlite3.c" /Fo:sqlite3.obj >nul 2>nul
        if exist "%SQLITE_DIR%\sqlite3.obj" (
            lib /OUT:sqlite3.lib /MACHINE:X64 /nologo sqlite3.obj >nul 2>nul
            echo   [√] sqlite3.lib 已生成
        )
        popd
    ) else (
        echo   [!] 未找到 cl.exe（需要 VS 开发者终端）
        echo        请运行 "Developer Command Prompt for VS 2022"（在开始菜单里），然后再次运行 DevEnvSetup.bat
        echo        或稍后再试 （或在本步骤，在 CMake 时如找不到会自动再次尝试）
    )
)

REM ============================================================
REM  [4/5] CMake 配置 + 构建 C++ 核心
REM ============================================================
echo.
echo [4/5] CMake + C++ 构建

set "BUILD_DIR=%PROJECT_DIR%\build"

where cmake >nul 2>&1
if %errorlevel%==0 (
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    mkdir "%BUILD_DIR%"
    pushd "%BUILD_DIR%"

    echo   运行 CMake 配置...
    cmake -G "Visual Studio 17 2022" -A x64 ^
        -DWINFSP_DIR="!WINFSP_DIR!" ^
        -DSQLITE3_DIR="%SQLITE_DIR%" ^
        ..
    if errorlevel 1 (
        echo   [!] CMake 配置失败，请查看日志，可能是编译器或 WinFsp 路径问题
    ) else (
        echo   [√] CMake 配置完成
        echo   正在构建 Release...
        cmake --build . --config Release --parallel %NUMBER_OF_PROCESSORS%
        if errorlevel 1 (
            echo   [!] C++ 构建失败
        ) else (
            echo   [√] C++ 核心构建完成
        )
    )
    popd
) else (
    echo   [!] 未检测到 CMake，跳过 C++ 构建
)

REM ============================================================
REM  [5/5] 构建 WPF GUI
REM ============================================================
echo.
echo [5/5] 构建 WPF GUI

where dotnet >nul 2>&1
if %errorlevel%==0 (
    echo   dotnet build...
    pushd "%PROJECT_DIR%\GUI\RemoteGameHub.UI"
    dotnet build -c Release --nologo -v quiet
    if errorlevel 1 (
        echo   [!] WPF 构建失败
    ) else (
        echo   [√] WPF GUI 构建完成
    )
    popd
) else (
    echo   [!] 未检测到 dotnet，跳过 WPF 构建
)

REM ============================================================
REM  完成总结
REM ============================================================
echo.
echo ============================================================
echo   部署完成，请检查以下文件
echo ============================================================
echo.
echo   === 生成路径
echo     C++ 核心: %BUILD_DIR%\Release
echo     WPF GUI: %PROJECT_DIR%GUI\RemoteGameHub.UI\bin\Release\net8.0-windows
echo.
echo   === M1 快速测试
echo     cd %BUILD_DIR%
echo     Release\rgh_mount.exe 192.168.5.103 Games 子目录 G 用户名 密码
echo.
echo   === 快速测试命令
echo     .\QuickTest.bat
echo ============================================================
echo.

endlocal
