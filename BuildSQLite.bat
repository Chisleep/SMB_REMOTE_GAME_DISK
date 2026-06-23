@echo off
REM ============================================================
REM   把 SQLite amalgamation 直接编译成 sqlite3.lib
REM   本脚本不需要网络: 下载已经完成 sqlite3.c / sqlite3.h 合并到 ThirdParty/sqlite/
REM   通过本脚本能直接生成 sqlite3.lib, 不再需要用户下载预编译 DLL
REM ============================================================

setlocal EnableDelayedExpansion
cd /d "%~dp0"
set SQLITE_DIR=%~dp0ThirdParty\sqlite
set OUT_DIR=%SQLITE_DIR%\build

if not exist "%SQLITE_DIR%" (
    echo [错误] 未找到 %SQLITE_DIR%
    echo 请先运行 DevEnvSetup.bat 自动下载, 或手动把 sqlite3.c/sqlite3.h 放到该目录
    exit /b 1
)

REM 检查是否在 VS 开发者终端 (能找到 cl.exe)
where cl >nul 2>&1
if not %errorlevel%==0 (
    echo [提示] 未检测到 MSVC 编译器, 本脚本需在 "Developer Command Prompt for VS 2022" 中运行
    echo      也可以在 PowerShell 先运行:
    echo          'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
    exit /b 1
)

echo -------------------------------------------
echo  编译 SQLite amalgamation -> sqlite3.lib
echo  源目录: %SQLITE_DIR%
echo -------------------------------------------

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

pushd "%OUT_DIR%"

REM 1) 编译 sqlite3.c -> sqlite3.obj (Release, x64)
cl.exe /c /O2 /GL /GS- /MT /W4 /DNDEBUG /DSQLITE_THREADSAFE=1 /DSQLITE_ENABLE_FTS3 /DSQLITE_ENABLE_RTREE /I "%SQLITE_DIR%" "%SQLITE_DIR%\sqlite3.c" /Fo:sqlite3.obj
if not exist sqlite3.obj (
    echo [错误] sqlite3.obj 编译失败, 请检查日志
    popd
    exit /b 1
)

REM 2) 生成静态库 sqlite3.lib (不使用 DLL, 直接静态链接进程序)
lib.exe /OUT:sqlite3.lib /MACHINE:X64 sqlite3.obj

REM 3) 复制头文件到 build/ 方便引用
copy /y "%SQLITE_DIR%\sqlite3.h" .\ >nul

popd
echo.
echo [√] SQLite 编译完成, 产物:
echo     lib : %OUT_DIR%\sqlite3.lib
echo     头:  %OUT_DIR%\sqlite3.h
echo     现在可以在 CMake 配置时使用:
echo         -DSQLITE3_DIR="%OUT_DIR%"
echo.
endlocal
