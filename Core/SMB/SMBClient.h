#pragma once

#include "Common/Types.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rgh {

// 文件信息结构（跨平台抽象）
struct RemoteFileInfo {
    std::string name;
    uint64_t size = 0;
    bool isDirectory = false;
    bool isReadOnly = false;
    time_t createTime = 0;
    time_t modifyTime = 0;
    time_t accessTime = 0;
    uint32_t attributes = 0;
};

// SMB 文件客户端
// 封装对 SMB 共享路径的文件操作，使用 Windows 原生 API + UNC 路径
// 不引入第三方 SMB 库，依赖 Windows 内置 SMB 客户端
class SMBClient {
public:
    SMBClient(const SmbServerConfig& config);
    ~SMBClient();

    // 建立到 SMB 共享的连接（建立根目录句柄用于保活）
    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }

    // ---- 文件操作 ----

    // 打开文件，返回句柄
#ifdef _WIN32
    HANDLE openFile(const std::string& relativePath, DWORD access, DWORD share, DWORD disposition, DWORD flags);
#endif

    // 读取文件到缓冲区，返回实际读取字节数
    int64_t readFile(const std::string& relativePath, uint64_t offset, void* buffer, uint32_t size);

    // 写入文件
    int64_t writeFile(const std::string& relativePath, uint64_t offset, const void* buffer, uint32_t size);

    // 查询文件信息
    bool getFileInfo(const std::string& relativePath, RemoteFileInfo& info);

    // 枚举目录
    bool listDirectory(const std::string& relativePath, std::vector<RemoteFileInfo>& entries);

    // 创建目录
    bool createDirectory(const std::string& relativePath);

    // 删除文件
    bool deleteFile(const std::string& relativePath);

    // 删除目录
    bool removeDirectory(const std::string& relativePath);

    // 重命名/移动
    bool rename(const std::string& oldPath, const std::string& newPath);

    // 获取完整 UNC 路径
    std::string getFullPath(const std::string& relativePath) const;

    // 测试连接延迟（毫秒）
    uint32_t pingLatencyMs();

private:
    SmbServerConfig config_;
    bool connected_ = false;

#ifdef _WIN32
    HANDLE rootHandle_ = INVALID_HANDLE_VALUE; // 根目录句柄，用于保活检测
#endif

    // 确保已连接
    bool ensureConnected();
};

} // namespace rgh
