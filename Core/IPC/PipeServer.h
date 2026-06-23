#pragma once

#include "IPC/JsonProtocol.h"
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <functional>
#include <atomic>
#include <queue>
#include <condition_variable>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rgh {

// 命名管道服务器
// C++ 核心服务端，接收 C# GUI 的请求并返回响应
// 支持双向通信：GUI 可订阅事件通知（如启动进度、游戏退出）
class PipeServer {
public:
    static PipeServer& instance();

    // 启动管道服务器
    bool start(const std::string& pipeName = "\\\\.\\pipe\\RemoteGameHub");
    void stop();

    // 请求处理器类型
    // GUI 发送请求时，调用此处理器生成响应
    using RequestHandler = std::function<IpcResponse(const IpcRequest&)>;
    void setRequestHandler(RequestHandler handler) { requestHandler_ = handler; }

    // 事件通知（服务端 -> GUI 推送）
    // 用于启动进度、游戏退出等异步事件
    using ClientId = int;

    void broadcastEvent(const IpcResponse& event);
    void sendToClient(ClientId clientId, const IpcResponse& response);

    // 服务器状态
    bool isRunning() const { return running_; }
    int connectedClients() const;

    // 管道默认名称
    static constexpr const char* DefaultPipeName = "\\\\.\\pipe\\RemoteGameHub";

private:
    PipeServer() = default;

    void acceptLoop();
    void clientLoop(ClientId id
#ifdef _WIN32
        , HANDLE pipe
#endif
    );

    std::string pipeName_;
    std::atomic<bool> running_{false};
    RequestHandler requestHandler_;

    std::thread acceptThread_;
    std::vector<std::thread> clientThreads_;

#ifdef _WIN32
    struct ClientInfo {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        std::atomic<bool> active{false};
    };
    std::vector<std::unique_ptr<ClientInfo>> clients_;
#endif

    std::mutex clientsMutex_;
    ClientId nextClientId_ = 1;
};

// 命名管道客户端（供测试用，GUI 端用 C# 实现）
class PipeClient {
public:
    bool connect(const std::string& pipeName = PipeServer::DefaultPipeName);
    void disconnect();
    bool isConnected() const { return connected_; }

    // 发送请求并等待响应
    IpcResponse sendRequest(const IpcRequest& request, int timeoutMs = 5000);

    // 设置事件回调（接收服务端推送的事件）
    using EventHandler = std::function<void(const IpcResponse&)>;
    void setEventHandler(EventHandler handler) { eventHandler_ = handler; }

    // 启动事件监听线程
    void startEventLoop();
    void stopEventLoop();

private:
    bool connected_ = false;
    EventHandler eventHandler_;
    std::thread eventThread_;
    std::atomic<bool> listening_{false};

#ifdef _WIN32
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
#endif
};

} // namespace rgh
