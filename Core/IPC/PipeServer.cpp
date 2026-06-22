#include "IPC/PipeServer.h"
#include "Common/Logger.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <sstream>
#include <chrono>

namespace rgh {

// ===== PipeServer =====

PipeServer& PipeServer::instance() {
    static PipeServer server;
    return server;
}

bool PipeServer::start(const std::string& pipeName) {
    if (running_) return true;
    pipeName_ = pipeName;
    running_ = true;

    acceptThread_ = std::thread(&PipeServer::acceptLoop, this);

    RGH_LOG_INFO("PipeServer", "管道服务器已启动: " + pipeName_);
    return true;
}

void PipeServer::stop() {
    if (!running_) return;
    running_ = false;

#ifdef _WIN32
    // 关闭所有客户端连接
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& client : clients_) {
            client->active = false;
            if (client->pipe != INVALID_HANDLE_VALUE) {
                DisconnectNamedPipe(client->pipe);
                CloseHandle(client->pipe);
            }
        }
        clients_.clear();
    }
#endif

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    for (auto& t : clientThreads_) {
        if (t.joinable()) t.join();
    }
    clientThreads_.clear();

    RGH_LOG_INFO("PipeServer", "管道服务器已停止");
}

#ifdef _WIN32

void PipeServer::acceptLoop() {
    while (running_) {
        // 创建命名管道实例
        HANDLE pipe = CreateNamedPipeA(
            pipeName_.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64 * 1024,  // 输出缓冲区
            64 * 1024,  // 输入缓冲区
            0,          // 默认超时
            nullptr);   // 默认安全属性

        if (pipe == INVALID_HANDLE_VALUE) {
            RGH_LOG_ERROR("PipeServer", "CreateNamedPipe 失败: " + std::to_string(GetLastError()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        // 等待客户端连接（异步）
        OVERLAPPED ov = {};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &ov);

        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // 等待连接
                HANDLE handles[] = { ov.hEvent };
                DWORD waitResult = WaitForMultipleObjects(1, handles, FALSE, 1000);

                if (waitResult == WAIT_TIMEOUT) {
                    // 超时，检查是否仍在运行
                    CancelIo(pipe);
                    CloseHandle(ov.hEvent);
                    CloseHandle(pipe);
                    continue;
                }
                if (waitResult != WAIT_OBJECT_0) {
                    CloseHandle(ov.hEvent);
                    CloseHandle(pipe);
                    continue;
                }
                DWORD bytesTransferred;
                if (!GetOverlappedResult(pipe, &ov, &bytesTransferred, FALSE)) {
                    CloseHandle(ov.hEvent);
                    CloseHandle(pipe);
                    continue;
                }
            } else if (err != ERROR_PIPE_CONNECTED) {
                CloseHandle(ov.hEvent);
                CloseHandle(pipe);
                continue;
            }
        }
        CloseHandle(ov.hEvent);

        if (!running_) {
            CloseHandle(pipe);
            break;
        }

        // 客户端已连接，启动处理线程
        ClientId id = nextClientId_++;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            auto clientInfo = std::make_unique<ClientInfo>();
            clientInfo->pipe = pipe;
            clientInfo->active = true;
            clients_.push_back(std::move(clientInfo));
        }

        clientThreads_.emplace_back(&PipeServer::clientLoop, this, id, pipe);
    }
}

void PipeServer::clientLoop(ClientId id, HANDLE pipe) {
    RGH_LOG_DEBUG("PipeServer", "客户端 #" + std::to_string(id) + " 已连接");

    char buffer[256 * 1024]; // 256KB 接收缓冲区

    while (running_) {
        // 读取请求
        DWORD bytesRead = 0;
        BOOL success = ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);

        if (!success || bytesRead == 0) {
            DWORD err = GetLastError();
            if (err != ERROR_BROKEN_PIPE) {
                RGH_LOG_DEBUG("PipeServer", "客户端 #" + std::to_string(id) + " 读取失败: " + std::to_string(err));
            }
            break;
        }

        buffer[bytesRead] = '\0';
        std::string requestJson(buffer, bytesRead);

        RGH_LOG_TRACE("PipeServer", "收到请求: " + requestJson);

        // 解析并处理请求
        IpcRequest req = JsonProtocol::parseRequest(requestJson);
        IpcResponse resp;

        if (requestHandler_) {
            resp = requestHandler_(req);
        } else {
            resp.action = IpcAction::Error;
            resp.success = false;
            resp.error = "No request handler set";
            resp.sessionId = req.sessionId;
        }

        // 发送响应
        std::string responseJson = JsonProtocol::serializeResponse(resp);
        DWORD bytesWritten = 0;
        success = WriteFile(pipe, responseJson.c_str(),
            static_cast<DWORD>(responseJson.size()), &bytesWritten, nullptr);

        if (!success) {
            RGH_LOG_WARN("PipeServer", "发送响应失败");
            break;
        }
    }

    // 清理
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.erase(
            std::remove_if(clients_.begin(), clients_.end(),
                [pipe](const std::unique_ptr<ClientInfo>& c) { return c->pipe == pipe; }),
            clients_.end());
    }

    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);

    RGH_LOG_DEBUG("PipeServer", "客户端 #" + std::to_string(id) + " 已断开");
}

void PipeServer::broadcastEvent(const IpcResponse& event) {
    std::string json = JsonProtocol::serializeResponse(event);

    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& client : clients_) {
        if (client->active && client->pipe != INVALID_HANDLE_VALUE) {
            DWORD bytesWritten = 0;
            WriteFile(client->pipe, json.c_str(),
                static_cast<DWORD>(json.size()), &bytesWritten, nullptr);
        }
    }
}

void PipeServer::sendToClient(ClientId clientId, const IpcResponse& response) {
    std::string json = JsonProtocol::serializeResponse(response);
    std::lock_guard<std::mutex> lock(clientsMutex_);
    // 简化实现：广播（实际应按 clientId 索引）
    for (auto& client : clients_) {
        if (client->active && client->pipe != INVALID_HANDLE_VALUE) {
            DWORD bytesWritten = 0;
            WriteFile(client->pipe, json.c_str(),
                static_cast<DWORD>(json.size()), &bytesWritten, nullptr);
        }
    }
}

int PipeServer::connectedClients() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(clientsMutex_));
    int count = 0;
    for (const auto& c : clients_) {
        if (c->active) ++count;
    }
    return count;
}

#else // 非 Windows 桩实现

void PipeServer::acceptLoop() {}
void PipeServer::clientLoop(ClientId, void*) {}
void PipeServer::broadcastEvent(const IpcResponse&) {}
void PipeServer::sendToClient(ClientId, const IpcResponse&) {}
int PipeServer::connectedClients() const { return 0; }

#endif

// ===== PipeClient =====

bool PipeClient::connect(const std::string& pipeName) {
#ifdef _WIN32
    pipe_ = CreateFileA(
        pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);

    if (pipe_ == INVALID_HANDLE_VALUE) {
        RGH_LOG_ERROR("PipeClient", "连接管道失败: " + std::to_string(GetLastError()));
        return false;
    }

    // 设置消息模式
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr);

    connected_ = true;
    return true;
#else
    return false;
#endif
}

void PipeClient::disconnect() {
#ifdef _WIN32
    stopEventLoop();
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
#endif
    connected_ = false;
}

IpcResponse PipeClient::sendRequest(const IpcRequest& request, int timeoutMs) {
    IpcResponse resp;
    resp.action = IpcAction::Error;
    resp.success = false;
    resp.error = "Not implemented";

#ifdef _WIN32
    if (!connected_ || pipe_ == INVALID_HANDLE_VALUE) {
        resp.error = "Not connected";
        return resp;
    }

    std::string json = JsonProtocol::serializeRequest(request);

    // 同步写入
    DWORD bytesWritten = 0;
    BOOL success = WriteFile(pipe_, json.c_str(),
        static_cast<DWORD>(json.size()), &bytesWritten, nullptr);
    if (!success) {
        resp.error = "WriteFile failed: " + std::to_string(GetLastError());
        return resp;
    }

    // 同步读取响应
    char buffer[256 * 1024];
    DWORD bytesRead = 0;
    success = ReadFile(pipe_, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
    if (!success || bytesRead == 0) {
        resp.error = "ReadFile failed: " + std::to_string(GetLastError());
        return resp;
    }

    buffer[bytesRead] = '\0';
    resp = JsonProtocol::parseResponse(std::string(buffer, bytesRead));
#endif
    return resp;
}

void PipeClient::startEventLoop() {
#ifdef _WIN32
    listening_ = true;
    eventThread_ = std::thread([this]() {
        char buffer[256 * 1024];
        while (listening_ && pipe_ != INVALID_HANDLE_VALUE) {
            DWORD bytesRead = 0;
            BOOL success = ReadFile(pipe_, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
            if (!success || bytesRead == 0) break;

            buffer[bytesRead] = '\0';
            IpcResponse event = JsonProtocol::parseResponse(std::string(buffer, bytesRead));
            if (eventHandler_) {
                eventHandler_(event);
            }
        }
    });
#endif
}

void PipeClient::stopEventLoop() {
    listening_ = false;
    if (eventThread_.joinable()) {
        eventThread_.join();
    }
}

} // namespace rgh
