@echo off
REM ============================================================
REM   RemoteGameHub 快速验证 (M1/M2 虚拟盘测试)
REM   需要:
REM     1) 检查 WinFsp 是否已装好
REM     2) 测试 SMB 连接 192.168.5.103
REM     3) 挂载到指定目录到 G: 盘
REM     4) 启动一个已知 exe
REM ============================================================

setlocal EnableDelayedExpansion
chcp 65001 >nul
cd /d "%~dp0"

echo ============================================
echo   RemoteGameHub 快速验证
echo ============================================
echo.

REM ===== 参数默认配置 (用户可修改
set SMB_HOST=192.168.5.103
set SMB_SHARE=Games
set SMB_SUBDIR=
set /p "SMB_HOST=  SMB 服务器: "
set /p "SMB_SHARE=  SMB 共享名: "
set /p "SMB_SUBDIR=  游戏子目录 (例如 "
set /p "DRIVE=  盘符 (例如 G): "
set /p "USER=  用户名: "
set /p "PASS=  密码: "

echo.
echo ============================================
echo  参数
echo    %DRIVE%: -> \\%SMB_HOST%\%SMB_SHARE%\%SMB_SUBDIR%
echo    用户名: %USER%
echo ============================================
echo.

REM 检测 WinFsp
echo [1/4] 检查 WinFsp 安装状态
if exist "C:\Program Files (x86)\WinFsp\bin\fsreg.bat" (
    echo   [√] WinFsp 已安装
) else (
    echo   [!] 未检测到 WinFsp, 请先安装
    echo      命令: DevEnvSetup.bat 自动安装, 或手工下载 https://winfsp.dev
    pause
    exit /b 1
)

REM 检测 SMB 共享
echo.
echo [2/4] 测试 SMB 连接...
net use \\%SMB_HOST%\%SMB_SHARE% /user:%USER% "!PASS!" >nul 2>&1
if errorlevel 1 (
    echo   [!] 连接测试失败, 请检查 IP / 用户名 / 密码
) else (
    echo   [√] SMB 连接已连接成功
)

echo.
echo [3/4] 挂载虚拟盘...
echo.

REM 查找 rgh_mount.exe
set MOUNT_EXE=build\Release\rgh_mount.exe
if not exist "%MOUNT_EXE%" (
    set MOUNT_EXE=build\bin\Release\rgh_mount.exe
)
if not exist "%MOUNT_EXE%" (
    set MOUNT_EXE=build\bin\rgh_mount.exe
)

if not exist "%MOUNT_EXE%" (
    echo   [!] 未找到 rgh_mount.exe
    echo      请先运行 BuildAll.bat 编译
    pause
    exit /b 1
)

echo   启动 %MOUNT_EXE%
echo.

"%MOUNT_EXE%" "%SMB_HOST%" "%SMB_SHARE%" "%SMB_SUBDIR%" "%DRIVE%" "%USER%" "!PASS!"

echo.
echo ============================================
echo  现在可以:
echo    - 打开资源管理器, 进入 %DRIVE%:\
echo    - 双击 *.exe 测试游戏
echo    - Ctrl+C 终止程序卸载
echo ============================================

net use \\%SMB_HOST%\%SMB_SHARE% /delete >nul 2>&1

endlocal
