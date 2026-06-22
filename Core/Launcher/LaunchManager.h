#pragma once

#include "Common/Types.h"
#include "VirtualDisk/VirtualDiskEngine.h"
#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rgh {

// 启动管理器
// 管理游戏启动全流程：挂载虚拟盘 -> 启动进程 -> 监控 -> 退出清理
class LaunchManager {
public:
    static LaunchManager& instance();

    // 启动游戏
    // 返回 sessionId，用于跟踪本次启动
    std::string launchGame(const GameInfo& game,
                           std::function<void(LaunchStatus, const std::string&)> progress = nullptr);

    // 终止当前运行的游戏
    bool terminateGame();

    // 当前是否有游戏在运行
    bool isGameRunning() const { return running_.load(); }

    // 获取当前运行的游戏信息
    const GameInfo& currentGame() const { return currentGame_; }

    // 获取当前 sessionId
    std::string currentSessionId() const { return sessionId_; }

    // 设置虚拟磁盘引擎
    void setVirtualDiskEngine(VirtualDiskEngine* engine) { vdEngine_ = engine; }

    // 启动状态回调
    using StatusCallback = std::function<void(const std::string& sessionId, LaunchStatus, const std::string&)>;
    void setStatusCallback(StatusCallback cb) { statusCallback_ = cb; }

private:
    LaunchManager() = default;

    // 启动流程各阶段
    bool checkConnection();
    bool mountVirtualDisk(const GameInfo& game);
    bool launchProcess(const GameInfo& game);
    void monitorProcess();
    void cleanup();

    VirtualDiskEngine* vdEngine_ = nullptr;
    StatusCallback statusCallback_;

    std::atomic<bool> running_{false};
    std::atomic<bool> terminating_{false};
    GameInfo currentGame_;
    std::string sessionId_;

#ifdef _WIN32
    HANDLE processHandle_ = nullptr;
    HANDLE processThread_ = nullptr;
    DWORD processId_ = 0;
#endif

    std::thread monitorThread_;
    std::mutex mutex_;

    void notifyStatus(LaunchStatus status, const std::string& msg);
};

} // namespace rgh
