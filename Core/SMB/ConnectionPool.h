#pragma once

#include "SMB/SMBClient.h"
#include "Common/Types.h"
#include <vector>
#include <mutex>
#include <memory>
#include <condition_variable>

namespace rgh {

// SMB 连接池
// 维护多个 SMBClient 实例，支持并发文件操作
// 游戏运行时可能同时读取多个文件，连接池避免单连接瓶颈
class ConnectionPool {
public:
    static ConnectionPool& instance();

    // 初始化连接池
    void init(const SmbServerConfig& config, int poolSize = 4);

    // 获取一个可用的 SMBClient（阻塞直到有可用连接）
    std::shared_ptr<SMBClient> acquire();

    // 归还连接
    void release(std::shared_ptr<SMBClient> client);

    // 关闭所有连接
    void shutdown();

    // 连接池状态
    int totalConnections() const { return static_cast<int>(clients_.size()); }
    int availableConnections() const;

    // RAII 借用器
    class Borrower {
    public:
        Borrower(ConnectionPool& pool) : pool_(&pool), client_(pool.acquire()) {}
        ~Borrower() { if (pool_ && client_) pool_->release(client_); }
        Borrower(Borrower&& o) noexcept : pool_(o.pool_), client_(std::move(o.client_)) { o.pool_ = nullptr; }
        Borrower(const Borrower&) = delete;
        Borrower& operator=(const Borrower&) = delete;

        SMBClient* operator->() { return client_.get(); }
        SMBClient& operator*() { return *client_; }
        explicit operator bool() const { return client_ != nullptr; }

    private:
        ConnectionPool* pool_;
        std::shared_ptr<SMBClient> client_;
    };

    // 便捷借用
    Borrower borrow() { return Borrower(*this); }

private:
    ConnectionPool() = default;

    SmbServerConfig config_;
    std::vector<std::shared_ptr<SMBClient>> clients_;
    std::vector<bool> inUse_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool initialized_ = false;
};

} // namespace rgh
