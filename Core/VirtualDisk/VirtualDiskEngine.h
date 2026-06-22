#pragma once

#include "Common/Types.h"
#include "SMB/SMBClient.h"
#include <string>
#include <functional>
#include <atomic>
#include <memory>

#ifdef _WIN32
#include <winfsp/winfsp.h>
#endif

namespace rgh {

// 虚拟磁盘挂载状态
enum class MountStatus {
    Unmounted,
    Mounting,
    Mounted,
    Unmounting,
    Failed
};

// 虚拟磁盘引擎
// 基于 WinFsp 创建用户态文件系统，将 SMB 远程目录映射为本地盘符
// 游戏看到的是本地物理磁盘，绕过网络路径检测
class VirtualDiskEngine {
public:
    VirtualDiskEngine();
    ~VirtualDiskEngine();

    // 挂载虚拟磁盘
    // smbRootPath: SMB 上的游戏根目录相对路径，如 "ActionGame"
    // driveLetter: 盘符，如 'G'
    bool mount(const std::string& smbRootPath, char driveLetter);

    // 卸载虚拟磁盘
    bool unmount();

    // 挂载状态
    MountStatus status() const { return status_; }
    bool isMounted() const { return status_ == MountStatus::Mounted; }

    // 获取挂载点路径，如 "G:"
    std::string mountPoint() const { return mountPoint_; }

    // 获取统计信息
    struct IOStats {
        std::atomic<uint64_t> totalReads{0};
        std::atomic<uint64_t> totalWrites{0};
        std::atomic<uint64_t> bytesRead{0};
        std::atomic<uint64_t> bytesWritten{0};
        std::atomic<uint64_t> openFiles{0};
        std::atomic<uint64_t> dirListings{0};
    };

    const IOStats& stats() const { return stats_; }

    // 设置状态回调（用于 GUI 显示挂载进度）
    void setStatusCallback(std::function<void(MountStatus, const std::string&)> callback) {
        statusCallback_ = callback;
    }

private:
    MountStatus status_ = MountStatus::Unmounted;
    std::string mountPoint_;
    std::string smbRootPath_;
    std::function<void(MountStatus, const std::string&)> statusCallback_;

    IOStats stats_;

#ifdef _WIN32
    FSP_FILE_SYSTEM* fileSystem_ = nullptr;
    FSP_FSCTL_VOLUME_PARAMS volumeParams_{};

    // WinFsp 回调函数表
    FSP_FILE_OPERATION interface_;

    void initInterface();
    void notifyStatus(MountStatus status, const std::string& msg);
#endif
};

} // namespace rgh
