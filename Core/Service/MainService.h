#pragma once

#include "Common/Types.h"
#include "VirtualDisk/VirtualDiskEngine.h"
#include "IPC/PipeServer.h"
#include <memory>
#include <string>
#include <atomic>

namespace rgh {

// 主服务类
// 整合所有模块，管理服务生命周期
// 处理来自 GUI 的 IPC 请求
class MainService {
public:
    static MainService& instance();

    // 初始化服务
    bool init();

    // 启动服务（启动 IPC 服务器，等待 GUI 请求）
    void run();

    // 停止服务
    void stop();

    // 服务状态
    bool isRunning() const { return running_.load(); }

    // 获取服务状态信息（用于 GUI 显示）
    struct ServiceStatus {
        bool smbConnected = false;
        uint32_t smbLatencyMs = 0;
        bool diskMounted = false;
        std::string mountPoint;
        bool gameRunning = false;
        std::string currentGameName;
        int connectedClients = 0;
    };

    ServiceStatus getStatus();

private:
    MainService() = default;

    // 初始化各子模块
    bool initLogger();
    bool initConfig();
    bool initDatabase();
    bool initSmbConnection();
    bool initVirtualDisk();
    bool initIpcServer();

    // IPC 请求处理器
    IpcResponse handleRequest(const IpcRequest& req);

    // 各 action 处理函数
    IpcResponse handlePing(const IpcRequest& req);
    IpcResponse handleGetStatus(const IpcRequest& req);
    IpcResponse handleTestSmb(const IpcRequest& req);
    IpcResponse handleSaveConfig(const IpcRequest& req);
    IpcResponse handleLoadConfig(const IpcRequest& req);
    IpcResponse handleScanGames(const IpcRequest& req);
    IpcResponse handleAddGame(const IpcRequest& req);
    IpcResponse handleRemoveGame(const IpcRequest& req);
    IpcResponse handleUpdateGame(const IpcRequest& req);
    IpcResponse handleGetGameList(const IpcRequest& req);
    IpcResponse handleLaunchGame(const IpcRequest& req);
    IpcResponse handleTerminateGame(const IpcRequest& req);
    IpcResponse handleGetStats(const IpcRequest& req);
    IpcResponse handleGetDiskStats(const IpcRequest& req);

    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};

    std::unique_ptr<VirtualDiskEngine> vdEngine_;
};

} // namespace rgh
