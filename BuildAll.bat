@echo off
REM ============================================================
REM   RemoteGameHub 一键编译脚本 (Windows 10/11 + VS2022)
REM   自动:
REM     1) 搜索可用的 VS2022 vcvars64.bat
REM     2) 初始化 SQLite (从 amalgamation 生成 sqlite3.lib)
REM     3) CMake 配置 + 编译 C++ 核心
REM     4) dotnet 编译 WPF GUI
REM ============================================================

setlocal EnableDelayedExpansion
chcp 65001 >nul

cd /d "%~dp0"
set PROJECT_DIR=%~dp0
set BUILD_DIR=%PROJECT_DIR%build
set LOG_FILE=%PROJECT_DIR%BuildAll.log

echo ============================================
echo   RemoteGameHub 一键编译 (Release x64)
echo ============================================
echo 项目: %PROJECT_DIR%
echo 日志: %LOG_FILE%
echo.

echo [时间] %date% %time% >"%LOG_FILE%"

REM ============================================================
REM  1. 寻找并初始化 VS2022 vcvars64.bat
REM ============================================================
echo [1/4] 初始化 MSVC 环境...
set VC_VARS=

REM 常见安装路径 (2022 Community/Professional/Enterprise/Preview/BuildTools)
set "VS_PATHS=^
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" ^
"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" ^
"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" ^
"C:\Program Files\Microsoft Visual Studio\2022\Preview\VC\Auxiliary\Build\vcvars64.bat" ^
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat""

for %%p in (%VS_PATHS%) do (
    if exist %%p (
        set "VC_VARS=%%~p"
        goto :vc_found
    )
)

REM 用 vswhere.exe 查找 (更可靠)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq delims=" %%i in (
        `"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -version [17^,18^) -property installationPath -latest`
    ) do (
        set "CANDIDATE=%%i\VC\Auxiliary\Build\vcvars64.bat"
        if exist "!CANDIDATE!" (
            set "VC_VARS=!CANDIDATE!"
        )
    )
)

:vc_found
if "%VC_VARS%"=="" (
    echo   [X] 未找到 Visual Studio 2022 的 vcvars64.bat
    echo   请先安装 Visual Studio 2022 或 Build Tools, 或手动指定路径
    echo   下载: https://visualstudio.microsoft.com/downloads/
    exit /b 1
)
echo   [√] VS: %VC_VARS%
call "%VC_VARS%" >>"%LOG_FILE%" 2>&1

REM ============================================================
REM  2. 生成 SQLite
REM ============================================================
echo [2/4] 编译 SQLite (amalgamation -> sqlite3.lib)...
if not exist "%PROJECT_DIR%ThirdParty\sqlite\sqlite3.c" (
    echo   [!] 缺少 sqlite3.c, 正在尝试自动下载...
    call "%PROJECT_DIR%DevEnvSetup.bat"
)
if exist "%PROJECT_DIR%ThirdParty\sqlite\sqlite3.c" (
    if not exist "%PROJECT_DIR%ThirdParty\sqlite\build\sqlite3.lib" (
        call "%PROJECT_DIR%BuildSQLite.bat"
    )
    if exist "%PROJECT_DIR%ThirdParty\sqlite\build\sqlite3.lib" (
        echo   [√] SQLite 可用
        set "SQLITE_DIR=%PROJECT_DIR%ThirdParty\sqlite\build"
    ) else (
        echo   [!] SQLite 编译失败, 尝试 DevEnvSetup.bat 下载的 sqlite3.lib
        if exist "%PROJECT_DIR%ThirdParty\sqlite\sqlite3.lib" (
            set "SQLITE_DIR=%PROJECT_DIR%ThirdParty\sqlite"
        )
    )
)

REM 自动尝试 DevEnvSetup 下载的 sqlite3.lib 路径
if "%SQLITE_DIR%"=="" (
    if exist "%PROJECT_DIR%ThirdParty\sqlite\sqlite3.lib" (
        set "SQLITE_DIR=%PROJECT_DIR%ThirdParty\sqlite"
    )
)

if "%SQLITE_DIR%"=="" (
    echo   [!] SQLite 不可用, 跳过游戏库相关功能
) else (
    echo   [√] SQLite 路径: %SQLITE_DIR%
)

REM ============================================================
REM  3. CMake 编译 C++ 核心
REM ============================================================
echo [3/4] 编译 C++ 核心服务 (WinFsp 虚拟磁盘)...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

pushd "%BUILD_DIR%"

echo   - CMake 配置 (Release x64)...
if "%SQLITE_DIR%"=="" (
    cmake -G "Visual Studio 17 2022" -A x64 "%PROJECT_DIR%" >>"%LOG_FILE%" 2>&1
) else (
    cmake -G "Visual Studio 17 2022" -A x64 -DSQLITE3_DIR="%SQLITE_DIR%" "%PROJECT_DIR%" >>"%LOG_FILE%" 2>&1
)

if errorlevel 1 (
    echo   [X] CMake 配置失败, 查看 %LOG_FILE%
    popd
    exit /b 1
)
echo   [√] CMake 配置完成

echo   - CMake 构建 Release (并行多核心)...
cmake --build . --config Release --parallel %NUMBER_OF_PROCESSORS% >>"%LOG_FILE%" 2>&1
if errorlevel 1 (
    echo   [!] 编译警告/错误, 查看 %LOG_FILE%
) else (
    echo   [√] C++ 核心构建完成
)
popd

REM ============================================================
REM  4. 编译 WPF GUI
REM ============================================================
echo [4/4] 编译 WPF GUI (.NET 8.0)...
where dotnet >nul 2>&1
if %errorlevel%==0 (
    pushd "%PROJECT_DIR%GUI\RemoteGameHub.UI"
    dotnet build -c Release --nologo -v minimal >>"%LOG_FILE%" 2>&1
    if errorlevel 1 (
        echo   [!] WPF 构建失败
    ) else (
        echo   [√] WPF GUI 构建完成
    )
    popd
) else (
    echo   [!] 未检测到 dotnet (需要 .NET 8 SDK), 已跳过 GUI 构建
)

REM ============================================================
REM  产物列出
REM ============================================================
echo.
echo ============================================
echo   编译完成, 产物
echo ============================================
echo.
dir /b "%BUILD_DIR%\Release" 2>nul
if exist "%PROJECT_DIR%GUI\RemoteGameHub.UI\bin\Release\net8.0-windows\RemoteGameHub.UI.exe" (
    echo   GUI: %PROJECT_DIR%GUI\RemoteGameHub.UI\bin\Release\net8.0-windows\RemoteGameHub.UI.exe
)
echo.
echo ============================================
echo  下一步 (测试 M1 虚拟盘):
echo     cd %BUILD_DIR%
echo     Release\rgh_mount.exe 192.168.5.103 Games 游戏目录名 G 用户名 密码
echo ============================================
echo.
endlocal
