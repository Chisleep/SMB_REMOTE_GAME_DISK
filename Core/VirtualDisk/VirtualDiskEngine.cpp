#include "VirtualDisk/VirtualDiskEngine.h"
#include "VirtualDisk/FileCallbacks.h"
#include "VirtualDisk/MemoryCache.h"
#include "SMB/ConnectionPool.h"
#include "Common/Logger.h"
#include "Common/Config.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <sstream>

namespace rgh {

std::string FileCallbacks::smbRootPath_;

VirtualDiskEngine::VirtualDiskEngine() {
#ifdef _WIN32
    initInterface();
#endif
}

VirtualDiskEngine::~VirtualDiskEngine() {
    if (isMounted()) {
        unmount();
    }
}

#ifdef _WIN32

void VirtualDiskEngine::initInterface() {
    memset(&interface_, 0, sizeof(interface_));

    interface_.GetVolumeInfo = FileCallbacks::getVolumeInfo;
    interface_.SetVolumeLabel = FileCallbacks::setVolumeLabel;
    interface_.GetSecurityByName = FileCallbacks::getSecurityByName;
    interface_.Create = FileCallbacks::create;
    interface_.Open = FileCallbacks::open;
    interface_.Overwrite = FileCallbacks::overwrite;
    interface_.Cleanup = FileCallbacks::cleanup;
    interface_.Close = FileCallbacks::close;
    interface_.Read = FileCallbacks::read;
    interface_.Write = FileCallbacks::write;
    interface_.Flush = FileCallbacks::flush;
    interface_.GetFileInfo = FileCallbacks::getFileInfo;
    interface_.SetFileInfo = FileCallbacks::setFileInfo;
    interface_.CanDelete = FileCallbacks::canDelete;
    interface_.Rename = FileCallbacks::rename;
    interface_.ReadDirectory = FileCallbacks::readDirectory;
    interface_.SetDelete = FileCallbacks::setDelete;
    // GetSecurity / SetSecurity 可选，暂不实现
}

void VirtualDiskEngine::notifyStatus(MountStatus status, const std::string& msg) {
    status_ = status;
    RGH_LOG_INFO("VirtualDisk", "状态变更: " + msg);
    if (statusCallback_) {
        statusCallback_(status, msg);
    }
}

bool VirtualDiskEngine::mount(const std::string& smbRootPath, char driveLetter) {
    if (isMounted()) {
        RGH_LOG_WARN("VirtualDisk", "虚拟盘已挂载，先卸载");
        unmount();
    }

    notifyStatus(MountStatus::Mounting, "正在挂载虚拟盘 " + std::string(1, driveLetter) + ":");

    smbRootPath_ = smbRootPath;
    FileCallbacks::setSmbRootPath(smbRootPath);

    // 构建挂载点路径
    mountPoint_ = std::string(1, driveLetter) + ":";

    // 检查盘符是否可用
    std::string rootPath = mountPoint_ + "\\";
    if (GetFileAttributesA(rootPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        RGH_LOG_ERROR("VirtualDisk", "盘符已被占用: " + mountPoint_);
        notifyStatus(MountStatus::Failed, "盘符 " + mountPoint_ + " 已被占用");
        return false;
    }

    // 配置卷参数
    memset(&volumeParams_, 0, sizeof(volumeParams_));
    volumeParams_.SectorSize = 4096;
    volumeParams_.SectorsPerAllocationUnit = 1;
    volumeParams_.VolumeCreationTime = static_cast<UINT64>(time(nullptr)) * 10000000 + 116444736000000000ULL;
    volumeParams_.VolumeSerialNumber = 0x52474800; // "RGH\0"
    volumeParams_.FileInfoTimeout = 1000;          // 文件信息缓存1秒
    volumeParams_.CaseSensitiveSearch = 0;
    volumeParams_.CasePreservedNames = 1;
    volumeParams_.UnicodeOnDisk = 1;
    volumeParams_.PersistentAcls = 0;
    volumeParams_.ReparsePoints = 0;
    volumeParams_.ReparsePointsAccessCheck = 0;
    volumeParams_.NamedStreams = 0;
    volumeParams_.ReadOnlyVolume = 0;
    volumeParams_.PostCleanupWhenModifiedOnly = 1;
    volumeParams_.PassQueryDirectoryPattern = 1;
    volumeParams_.DeviceControl = 0;
    volumeParams_.UmFileContextIsUserContext2 = 1;

    // 卷标签
    const char* volumeLabel = "RemoteGameHub";
    NTSTATUS result = FspFileSystemCreate(
        L"" FSP_FSCTL_DISK_DEVICE_NAME,
        &volumeParams_,
        &interface_,
        &fileSystem_);

    if (result != 0) {
        RGH_LOG_ERROR("VirtualDisk", "FspFileSystemCreate 失败，NTSTATUS: 0x" +
            std::to_string(static_cast<unsigned long>(result)));
        notifyStatus(MountStatus::Failed, "创建文件系统失败");
        return false;
    }

    // 设置挂载点
    std::wstring wMountPoint(mountPoint_.begin(), mountPoint_.end());
    FspFileSystemSetMountPoint(fileSystem_, const_cast<PWSTR>(wMountPoint.c_str()));

    // 设置前缀（UNC 前缀，使游戏认为是本地磁盘）
    FspFileSystemSetPrefix(fileSystem_, nullptr, nullptr);

    // 启动文件系统分发器
    result = FspFileSystemStartDispatcher(fileSystem_, 0);
    if (result != 0) {
        RGH_LOG_ERROR("VirtualDisk", "FspFileSystemStartDispatcher 失败，NTSTATUS: 0x" +
            std::to_string(static_cast<unsigned long>(result)));
        FspFileSystemDelete(fileSystem_);
        fileSystem_ = nullptr;
        notifyStatus(MountStatus::Failed, "启动文件系统分发器失败");
        return false;
    }

    notifyStatus(MountStatus::Mounted, "虚拟盘已挂载: " + mountPoint_);
    RGH_LOG_INFO("VirtualDisk", "虚拟盘挂载成功: " + mountPoint_ + " -> \\\\" +
        Config::instance().smbConfig().host + "\\" +
        Config::instance().smbConfig().shareName + "\\" + smbRootPath);
    return true;
}

bool VirtualDiskEngine::unmount() {
    if (!fileSystem_) return true;

    notifyStatus(MountStatus::Unmounting, "正在卸载虚拟盘: " + mountPoint_);

    FspFileSystemStopDispatcher(fileSystem_);
    FspFileSystemDelete(fileSystem_);
    fileSystem_ = nullptr;

    // 清理内存缓存
    MemoryCache::instance().clear();

    notifyStatus(MountStatus::Unmounted, "虚拟盘已卸载: " + mountPoint_);
    mountPoint_.clear();
    smbRootPath_.clear();
    return true;
}

#else // 非 Windows 桩实现

bool VirtualDiskEngine::mount(const std::string& smbRootPath, char driveLetter) {
    RGH_LOG_WARN("VirtualDisk", "非 Windows 平台，虚拟盘挂载不可用");
    return false;
}

bool VirtualDiskEngine::unmount() {
    return false;
}

#endif

} // namespace rgh
