#include "Launcher/LaunchManager.h"
#include "SMB/ConnectionPool.h"
#include "Stats/StatsRecorder.h"
#include "Common/Logger.h"
#include "Common/Config.h"
#include "IPC/JsonProtocol.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <chrono>
#include <sstream>
#include <random>

namespace rgh {

LaunchManager& LaunchManager::instance() {
    static LaunchManager mgr;
    return mgr;
}

void LaunchManager::notifyStatus(LaunchStatus status, const std::string& msg) {
    RGH_LOG_INFO("LaunchManager", "状态: " + msg);
    if (statusCallback_) {
        statusCallback_(sessionId_, status, msg);
    }
}

bool LaunchManager::checkConnection() {
    auto borrower = ConnectionPool::instance().borrow();
    if (!borrower || !borrower->isConnected()) {
        notifyStatus(LaunchStatus::Failed, "SMB 连接不可用");
        return false;
    }
    return true;
}

bool LaunchManager::mountVirtualDisk(const GameInfo& game) {
    if (!vdEngine_) {
        notifyStatus(LaunchStatus::Failed, "虚拟磁盘引擎未初始化");
        return false;
    }

    // 挂载游戏目录到虚拟盘
    char driveLetter = Config::instance().diskConfig().driveLetter;
    if (!vdEngine_->mount(game.smbPath, driveLetter)) {
        notifyStatus(LaunchStatus::Failed, "虚拟盘挂载失败");
        return false;
    }

    // 等待盘符就绪
    std::string rootPath = std::string(1, driveLetter) + ":\\";
    for (int i = 0; i < 50; ++i) {
#ifdef _WIN32
        if (GetFileAttributesA(rootPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            notifyStatus(LaunchStatus::MountingDisk, "虚拟盘已就绪: " + rootPath);
            return true;
        }
        Sleep(100);
#endif
    }

    notifyStatus(LaunchStatus::Failed, "虚拟盘就绪超时");
    return false;
}

bool LaunchManager::launchProcess(const GameInfo& game) {
#ifdef _WIN32
    // 构建完整路径：G:\game.exe
    char driveLetter = Config::instance().diskConfig().driveLetter;
    std::string exePath = std::string(1, driveLetter) + ":\\" + game.exeRelativePath;

    // 规范化路径分隔符
    std::replace(exePath.begin(), exePath.end(), '/', '\\');

    RGH_LOG_INFO("LaunchManager", "启动进程: " + exePath + " " + game.launchArgs);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    // 构建命令行
    std::string cmdLine = "\"" + exePath + "\"";
    if (!game.launchArgs.empty()) {
        cmdLine += " " + game.launchArgs;
    }

    // 创建进程
    BOOL success = CreateProcessA(
        nullptr,
        const_cast<LPSTR>(cmdLine.c_str()),
        nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | NORMAL_PRIORITY_CLASS,
        nullptr,
        nullptr, // 工作目录默认为系统目录
        &si, &pi);

    if (!success) {
        DWORD err = GetLastError();
        notifyStatus(LaunchStatus::Failed, "启动进程失败，错误码: " + std::to_string(err));
        return false;
    }

    processHandle_ = pi.hProcess;
    processThread_ = pi.hThread;
    processId_ = pi.dwProcessId;

    notifyStatus(LaunchStatus::Launching, "进程已启动 PID=" + std::to_string(processId_));
    return true;
#else
    notifyStatus(LaunchStatus::Failed, "非 Windows 平台");
    return false;
#endif
}

void LaunchManager::monitorProcess() {
#ifdef _WIN32
    notifyStatus(LaunchStatus::Running, "游戏运行中");

    // 等待进程退出
    WaitForSingleObject(processHandle_, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processHandle_, &exitCode);

    bool normalExit = (exitCode == 0 || exitCode == STILL_ACTIVE);
    if (terminating_) {
        normalExit = true; // 用户主动终止
    }

    RGH_LOG_INFO("LaunchManager", "进程退出，退出码: " + std::to_string(exitCode));

    // 记录游玩统计
    StatsRecorder::instance().endSession(sessionId_, normalExit);

    notifyStatus(LaunchStatus::Exiting, "游戏已退出");
#endif
}

void LaunchManager::cleanup() {
#ifdef _WIN32
    if (processHandle_) {
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
    }
    if (processThread_) {
        CloseHandle(processThread_);
        processThread_ = nullptr;
    }
    processId_ = 0;
#endif

    // 卸载虚拟盘
    if (vdEngine_ && vdEngine_->isMounted()) {
        vdEngine_->unmount();
    }

    running_ = false;
    terminating_ = false;
    sessionId_.clear();
    notifyStatus(LaunchStatus::Completed, "启动流程完成");
}

std::string LaunchManager::launchGame(const GameInfo& game,
                                       std::function<void(LaunchStatus, const std::string&)> progress) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_.load()) {
        RGH_LOG_WARN("LaunchManager", "已有游戏在运行");
        return "";
    }

    // 生成 sessionId
    sessionId_ = JsonProtocol::generateSessionId();
    currentGame_ = game;
    running_ = true;
    terminating_ = false;

    // 设置进度回调
    if (progress) {
        statusCallback_ = [progress](const std::string&, LaunchStatus s, const std::string& msg) {
            progress(s, msg);
        };
    }

    RGH_LOG_INFO("LaunchManager", "开始启动游戏: " + game.name + " (session=" + sessionId_ + ")");

    // 1. 检查连接
    notifyStatus(LaunchStatus::CheckingConnection, "检查 SMB 连接...");
    if (!checkConnection()) {
        running_ = false;
        return sessionId_;
    }

    // 2. 挂载虚拟盘
    notifyStatus(LaunchStatus::MountingDisk, "挂载虚拟盘...");
    if (!mountVirtualDisk(game)) {
        running_ = false;
        return sessionId_;
    }

    // 3. 启动进程
    notifyStatus(LaunchStatus::Launching, "启动游戏进程...");
    if (!launchProcess(game)) {
        cleanup();
        return sessionId_;
    }

    // 4. 开始记录统计
    StatsRecorder::instance().startSession(sessionId_, game.id);

    // 5. 启动监控线程
    monitorThread_ = std::thread(&LaunchManager::monitorProcess, this);

    return sessionId_;
}

bool LaunchManager::terminateGame() {
    if (!running_.load()) return false;

    terminating_ = true;

#ifdef _WIN32
    if (processHandle_) {
        RGH_LOG_INFO("LaunchManager", "终止游戏进程 PID=" + std::to_string(processId_));

        // 优雅终止：发送 WM_CLOSE
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            DWORD pid;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == static_cast<DWORD>(lParam)) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(processId_));

        // 等待 3 秒
        if (WaitForSingleObject(processHandle_, 3000) == WAIT_TIMEOUT) {
            // 强制终止
            TerminateProcess(processHandle_, 1);
        }
    }
#endif

    // 等待监控线程结束
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }

    cleanup();
    return true;
}

} // namespace rgh
