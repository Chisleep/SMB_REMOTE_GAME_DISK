#include "SMB/ConnectionPool.h"
#include "Common/Logger.h"

namespace rgh {

ConnectionPool& ConnectionPool::instance() {
    static ConnectionPool pool;
    return pool;
}

void ConnectionPool::init(const SmbServerConfig& config, int poolSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    clients_.clear();
    inUse_.clear();

    for (int i = 0; i < poolSize; ++i) {
        auto client = std::make_shared<SMBClient>(config);
        if (client->connect()) {
            clients_.push_back(client);
            inUse_.push_back(false);
        } else {
            RGH_LOG_WARN("ConnectionPool", "连接 #" + std::to_string(i) + " 建立失败");
        }
    }

    initialized_ = true;
    RGH_LOG_INFO("ConnectionPool", "连接池初始化完成，可用连接: " + std::to_string(clients_.size()));
}

std::shared_ptr<SMBClient> ConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() {
        for (size_t i = 0; i < inUse_.size(); ++i) {
            if (!inUse_[i]) return true;
        }
        return false;
    });

    for (size_t i = 0; i < inUse_.size(); ++i) {
        if (!inUse_[i]) {
            inUse_[i] = true;
            return clients_[i];
        }
    }
    return nullptr;
}

void ConnectionPool::release(std::shared_ptr<SMBClient> client) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < clients_.size(); ++i) {
        if (clients_[i] == client) {
            inUse_[i] = false;
            cv_.notify_one();
            return;
        }
    }
}

void ConnectionPool::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& client : clients_) {
        client->disconnect();
    }
    clients_.clear();
    inUse_.clear();
    initialized_ = false;
    RGH_LOG_INFO("ConnectionPool", "连接池已关闭");
}

int ConnectionPool::availableConnections() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    int count = 0;
    for (bool used : inUse_) {
        if (!used) ++count;
    }
    return count;
}

} // namespace rgh
