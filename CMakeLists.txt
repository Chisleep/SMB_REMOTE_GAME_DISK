cmake_minimum_required(VERSION 3.20)
project(RemoteGameHub VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 仅在 Windows 上构建核心服务
if(NOT WIN32)
    message(WARNING "RemoteGameHub Core 只能在 Windows 上构建。当前平台非 Windows，仅生成项目结构。")
    return()
endif()

# WinFsp SDK 路径（需根据实际安装位置调整）
set(WINFSP_DIR "" CACHE PATH "WinFsp SDK 安装路径")
if(NOT WINFSP_DIR)
    # 常见安装路径
    if(EXISTS "C:/Program Files (x86)/WinFsp")
        set(WINFSP_DIR "C:/Program Files (x86)/WinFsp")
    endif()
endif()

# SQLite 路径
set(SQLITE3_DIR "" CACHE PATH "SQLite 源码路径")

# ---- 核心静态库 ----
add_library(RemoteGameHub.Core STATIC
    Core/Common/Logger.cpp
    Core/Common/Config.cpp
    Core/Common/CredentialStore.cpp
    Core/SMB/SMBClient.cpp
    Core/SMB/ConnectionPool.cpp
    Core/VirtualDisk/VirtualDiskEngine.cpp
    Core/VirtualDisk/FileCallbacks.cpp
    Core/VirtualDisk/MemoryCache.cpp
    Core/GameLibrary/GameScanner.cpp
    Core/GameLibrary/GameDatabase.cpp
    Core/GameLibrary/CoverFetcher.cpp
    Core/Launcher/LaunchManager.cpp
    Core/Launcher/ProcessMonitor.cpp
    Core/Stats/StatsRecorder.cpp
    Core/IPC/PipeServer.cpp
    Core/IPC/JsonProtocol.cpp
)

target_include_directories(RemoteGameHub.Core PRIVATE
    Core/
    ${WINFSP_DIR}/inc
    ${SQLITE3_DIR}
)

target_link_libraries(RemoteGameHub.Core PRIVATE
    ${WINFSP_DIR}/lib/winfsp-x64.lib
    ${SQLITE3_DIR}/sqlite3.lib
    wincred        # Windows Credential Manager
    ws2_32          # Winsock
    shlwapi         # 路径操作
    version
)

# ---- 后台服务可执行 ----
add_executable(RemoteGameHub.Service WIN32
    Core/Service/MainService.cpp
    Core/Service/ServiceMain.cpp
)
target_link_libraries(RemoteGameHub.Service PRIVATE RemoteGameHub.Core)

# ---- 命令行测试工具（M1 阶段用于验证虚拟盘）----
add_executable(rgh_mount
    Core/Service/MountTool.cpp
)
target_link_libraries(rgh_mount PRIVATE RemoteGameHub.Core)
