cmake_minimum_required(VERSION 3.20)
project(RemoteGameHub VERSION 0.1.0 LANGUAGES C CXX)

# ===== 全局编译标志
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 仅在 Windows 上继续（Linux环境仅用于语法检查）
if(NOT WIN32)
    message(STATUS "[Info] 当前非 Windows 平台，RemoteGameHub 核心需要 Windows + WinFsp 运行。此处仅生成项目结构。")
    return()
endif()

# 统一使用 MSVC 编译选项
add_compile_options(
    /W3                # 3级警告
    /permissive-        # 标准符合性
    /EHsc                # C++ 异常
    /MP                  # 并行编译
    /D "_UNICODE         # 宽字符
    /D "UNICODE"
    /D "_CRT_SECURE_NO_WARNINGS"
    /D "WIN32_LEAN_AND_MEAN"
    /D "_WIN32_WINNT=0x0A00"  # Windows 10+
    /D "NOMINMAX"
)

# Release /MT -> DLL 使用静态 CRT, /MD -> 动态 CRT (默认) - 这里使用 /MD 以便和 .NET GUI 兼容
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# 输出统一输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ===== 依赖路径（允许命令行覆盖
#  cmake .. -DWINFSP_DIR="C:/Program Files (x86)/WinFsp" -DSQLITE3_DIR="D:/third_party/sqlite"
set(WINFSP_DIR "" CACHE PATH "WinFsp SDK 安装路径")
set(SQLITE3_DIR "" CACHE PATH "SQLite 头文件+lib 目录")

# 尝试自动探测 WinFsp 安装路径
if(NOT WINFSP_DIR)
    if(EXISTS "C:/Program Files (x86)/WinFsp")
        set(WINFSP_DIR "C:/Program Files (x86)/WinFsp")
    elseif(EXISTS "$ENV{ProgramFiles\(x86\)/WinFsp")
        set(WINFSP_DIR "$ENV{ProgramFiles\(x86\)/WinFsp")
    elseif(EXISTS "$ENV{ProgramFiles}/WinFsp")
        set(WINFSP_DIR "$ENV{ProgramFiles}/WinFsp")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/ThirdParty/winfsp")
        set(WINFSP_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/winfsp")
    endif()
endif()

if(NOT SQLITE3_DIR)
    if(EXISTS "${CMAKE_SOURCE_DIR}/ThirdParty/sqlite")
        set(SQLITE3_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/sqlite")
    endif()
endif()

message(STATUS "-------------------------------------------")
message(STATUS "  WinFsp 路径: ${WINFSP_DIR}")
message(STATUS "  SQLite 路径: ${SQLITE3_DIR}")
message(STATUS "-------------------------------------------")

if(NOT WINFSP_DIR)
    message(FATAL_ERROR "[错误] 未找到 WinFsp 安装。请安装 WinFsp, 或运行 DevEnvSetup.bat 自动安装。")
endif()
if(NOT SQLITE3_DIR)
    message(STATUS "[警告] 未找到 SQLite, 编译游戏库管理（游戏扫描/启动器数据库功能将跳过SQLite 相关模块会编译失败。请运行 DevEnvSetup.bat 自动下载。")
endif()

# ===== include_directories 全局
include_directories(
    ${CMAKE_SOURCE_DIR}/Core
    ${WINFSP_DIR}/inc
)

if(SQLITE3_DIR
    ${SQLITE3_DIR}
endif()

# ===== SQLit 核心静态库
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
    Core/Launcher/LaunchManager.cpp
    Core/Launcher/ProcessMonitor.cpp
    Core/Stats/StatsRecorder.cpp
    Core/IPC/JsonProtocol.cpp
    Core/IPC/PipeServer.cpp
)

target_link_libraries(RemoteGameHub.Core PUBLIC
    ${SQLITE3_DIR}/sqlite3.lib
    wincred
    ws2_32
    shlwapi
    version
    advapi32
    user32
    shell32
    kernel32
)

if(EXISTS ${WINFSP_DIR}/lib/winfsp-x64.lib)
    target_link_libraries(RemoteGameHub.Core PUBLIC ${WINFSP_DIR}/lib/winfsp-x64.lib)
    message(STATUS "[√] 链接 WinFsp x64 导入库: winfsp-x64.lib")
elseif(EXISTS ${WINFSP_DIR}/lib/winfsp-x86.lib)
    target_link_libraries(RemoteGameHub.Core PUBLIC ${WINFSP_DIR}/lib/winfsp-x86.lib)
    message(STATUS "[√] 链接 WinFsp x86 导入库: winfsp-x86.lib")
else()
    message(STATUS "[警告] 找不到 WinFsp .lib, 将在链接阶段可能报错， 请确认 ${WINFSP_DIR}/lib/winfsp-x64.lib")
endif()

# 游戏数据库模块 (条件编译
if(SQLITE3_DIR AND EXISTS "${SQLITE3_DIR}/sqlite3.h")
    target_sources(RemoteGameHub.Core PRIVATE Core/GameLibrary/GameDatabase.cpp Core/GameLibrary/CoverFetcher.cpp)
    message(STATUS "[√] 已包含 游戏库管理 (SQLite)")
else()
    message(STATUS "[警告] 缺少 sqlite3.h, 跳过游戏数据库模块")
endif()

# ===== 后台服务可执行
add_executable(RemoteGameHub.Service WIN32
    Core/Service/ServiceMain.cpp
)
target_link_libraries(RemoteGameHub.Service PRIVATE RemoteGameHub.Core)

# ===== M1 测试工具：虚拟盘挂载命令行
add_executable(rgh_mount
    Core/Service/MountTool.cpp
)
target_link_libraries(rgh_mount PRIVATE RemoteGameHub.Core)

message(STATUS "\n==============================================================")
message(STATUS "  RemoteGameHub 构建配置完成")
message(STATUS "  构建: cmake --build . --config Release")
message(STATUS "==============================================================")