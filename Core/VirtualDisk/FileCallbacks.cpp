#include "VirtualDisk/FileCallbacks.h"
#include "VirtualDisk/MemoryCache.h"
#include "SMB/ConnectionPool.h"
#include "Common/Logger.h"
#include "Common/Config.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstring>
#include <sstream>

namespace rgh {

std::string FileCallbacks::smbRootPath_;

// ===== 辅助函数 =====

std::string FileCallbacks::normalizePath(const char* fileName) {
    // WinFsp 传入的路径以 \ 开头，如 "\game.exe"
    // 去掉前导反斜杠
    std::string path = fileName ? fileName : "";
    if (!path.empty() && path[0] == '\\') {
        path = path.substr(1);
    }
    // 替换 / 为 \ （兼容）
    std::replace(path.begin(), path.end(), '/', '\\');
    return path;
}

std::string FileCallbacks::toSmbPath(const std::string& virtualPath) {
    // 将虚拟路径转换为 SMB 相对路径
    // 虚拟路径是相对于挂载根的，SMB 路径需要加上 smbRootPath_
    if (virtualPath.empty()) {
        return smbRootPath_;
    }
    if (smbRootPath_.empty()) {
        return virtualPath;
    }
    return smbRootPath_ + "\\" + virtualPath;
}

#ifdef _WIN32

void FileCallbacks::fillFileInfo(const RemoteFileInfo& remote, FSP_FSCTL_FILE_INFO* fileInfo) {
    auto timeToWinFileTime = [](time_t t) -> UINT64 {
        if (t == 0) return 0;
        return static_cast<UINT64>(t) * 10000000 + 116444736000000000ULL;
    };

    fileInfo->FileAttributes = remote.attributes;
    if (fileInfo->FileAttributes == 0) {
        fileInfo->FileAttributes = remote.isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    }
    fileInfo->ReparseTag = 0;
    fileInfo->AllocationSize = ((remote.size + 4095) / 4096) * 4096;
    fileInfo->FileSize = remote.size;
    fileInfo->CreationTime = timeToWinFileTime(remote.createTime);
    fileInfo->LastAccessTime = timeToWinFileTime(remote.accessTime);
    fileInfo->LastWriteTime = timeToWinFileTime(remote.modifyTime);
    fileInfo->ChangeTime = fileInfo->LastWriteTime;
    fileInfo->IndexNumber = 0;
}

// ===== WinFsp 回调实现 =====

NTSTATUS FileCallbacks::getVolumeInfo(FSP_FILE_SYSTEM* fs, FSP_FSCTL_VOLUME_INFO* volumeInfo) {
    volumeInfo->TotalSize = 1024ULL * 1024 * 1024 * 1024; // 1TB 虚拟大小
    volumeInfo->FreeSize = 512ULL * 1024 * 1024 * 1024;    // 512GB 可用
    const char* label = "RemoteGameHub";
    volumeInfo->VolumeLabelLength = static_cast<UINT16>(strlen(label));
    memcpy(volumeInfo->VolumeLabel, label, volumeInfo->VolumeLabelLength);
    return STATUS_SUCCESS;
}

NTSTATUS FileCallbacks::setVolumeLabel(FSP_FILE_SYSTEM* fs, const char* volumeLabel,
                                        FSP_FSCTL_VOLUME_INFO* volumeInfo) {
    // 只读卷，忽略
    return getVolumeInfo(fs, volumeInfo);
}

NTSTATUS FileCallbacks::getSecurityByName(FSP_FILE_SYSTEM* fs, const char* fileName,
                                           PUINT32 fileAttributes,
                                           PSECURITY_DESCRIPTOR securityDescriptor,
                                           size_t* securityDescriptorSize) {
    std::string vPath = normalizePath(fileName);
    std::string smbPath = toSmbPath(vPath);

    auto borrower = ConnectionPool::instance().borrow();
    if (!borrower) return STATUS_DEVICE_DOES_NOT_EXIST;

    RemoteFileInfo info;
    if (!borrower->getFileInfo(smbPath, info)) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (fileAttributes) {
        *fileAttributes = info.attributes;
        if (*fileAttributes == 0) {
            *fileAttributes = info.isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        }
    }

    // 返回默认安全描述符（简化处理）
    if (securityDescriptorSize && *securityDescriptorSize > 0) {
        *securityDescriptorSize = 0; // 不返回安全描述符，使用默认
    }

    return STATUS_SUCCESS;
}

NTSTATUS FileCallbacks::create(FSP_FILE_SYSTEM* fs, const char* fileName,
                                UINT32 createOptions, UINT32 grantedAccess,
                                UINT32 fileAttributes, PSECURITY_DESCRIPTOR securityDescriptor,
                                UINT64 allocationSize, PVOID* fileContext,
                                FSP_FSCTL_FILE_INFO* fileInfo) {
    std::string vPath = normalizePath(fileName);
    std::string smbPath = toSmbPath(vPath);

    auto ctx = std::make_unique<FileContext>();
    ctx->remotePath = smbPath;
    ctx->fileName = vPath;

    bool isDir = (createOptions & FILE_DIRECTORY_FILE) != 0;
    ctx->isDirectory = isDir;

    auto borrower = ConnectionPool::instance().borrow();
    if (!borrower) return STATUS_DEVICE_DOES_NOT_EXIST;

    if (isDir) {
        if (!borrower->createDirectory(smbPath)) {
            DWORD err = GetLastError();
            if (err == ERROR_ALREADY_EXISTS) {
                // 目录已存在，获取信息
            } else {
                return STATUS_ACCESS_DENIED;
            }
        }
    } else {
        // 创建文件
        HANDLE h = borrower->openFile(smbPath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            return STATUS_ACCESS_DENIED;
        }
        ctx->smbHandle = h;
    }

    RemoteFileInfo info;
    if (borrower->getFileInfo(smbPath, info)) {
        ctx->fileSize = info.size;
        ctx->attributes = info.attributes;
        ctx->createTime = info.createTime;
        ctx->modifyTime = info.modifyTime;
        ctx->accessTime = info.accessTime;
        fillFileInfo(info, fileInfo);
    }

    *fileContext = ctx.release();
    return STATUS_SUCCESS;
}

NTSTATUS FileCallbacks::open(FSP_FILE_SYSTEM* fs, const char* fileName,
                              UINT32 createOptions, UINT32 grantedAccess,
                              PVOID* fileContext, FSP_FSCTL_FILE_INFO* fileInfo) {
    std::string vPath = normalizePath(fileName);
    std::string smbPath = toSmbPath(vPath);

    auto borrower = ConnectionPool::instance().borrow();
    if (!borrower) return STATUS_DEVICE_DOES_NOT_EXIST;

    RemoteFileInfo info;
    if (!borrower->getFileInfo(smbPath, info)) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    auto ctx = std::make_unique<FileContext>();
    ctx->remotePath = smbPath;
    ctx->fileName = vPath;
    ctx->fileSize = info.size;
    ctx->isDirectory = info.isDirectory;
    ctx->attributes = info.attributes;
    ctx->createTime = info.createTime;
    ctx->modifyTime = info.modifyTime;
    ctx->accessTime = info.accessTime;

    if (!info.isDirectory) {
        // 打开文件句柄
        DWORD access = GENERIC_READ;
        if (grantedAccess & FILE_WRITE_DATA) access |= GENERIC_WRITE;
        HANDLE h = borrower->openFile(smbPath, access,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h != INVALID_HANDLE_VALUE) {
            ctx->smbHandle = h;
        }
    }

    fillFileInfo(info, fileInfo);
    *fileContext = ctx.release();
    return STATUS_SUCCESS;
}

NTSTATUS FileCallbacks::overwrite(FSP_FILE_SYSTEM* fs, PVOID fileContext,
                                   UINT32 fileAttributes, BOOLEAN replaceFileAttributes,
                                   UINT64 allocationSize, FSP_FSCTL_FILE_INFO* fileInfo) {
    auto* ctx = static_cast<FileContext*>(fileContext);
    if (!ctx) return STATUS_INVALID_PARAMETER;

    // 截断文件
    auto borrower = ConnectionPool::instance().borrow();
    if (!borrower) return STATUS_DEVICE_DOES_NOT_EXIST;

    // 使缓存失效
    MemoryCache::instance().invalidate(ctx->remotePath);

    // 重新打开并截断
    if (ctx->smbHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx->smbHandle);
    }
    ctx->smbHandle = borrower->openFile(ctx->remotePath, GENERIC_WRITE,
        FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);

    ctx->fileSize = 0;

    RemoteFileInfo info;
    if (borrower->getFileInfo(ctx->remotePath, info)) {
        fillFileInfo(info, fileInfo);
    }
    return STATUS_SUCCESS;
}

void FileCallbacks::cleanup(FSP_FILE_SYSTEM* fs, PVOID fileContext,
                             const char* fileName, BOOLEAN deleteFile) {
    auto* ctx = static_cast<FileContext*>(fileContext);
    if (!ctx) return;

    if (deleteFile) {
        auto borrower = ConnectionPool::instance().borrow();
        if (borrower) {
            if (ctx->isDirectory) {
                borrower->removeDirectory(ctx->remotePath);
            } else {
                borrower->deleteFile(ctx->remotePath);
            }
        }
        MemoryCache::instance().invalidate(ctx->remotePath);
    }
}

void FileCallbacks::close(FSP_FILE_SYSTEM* fs, PVOID fileContext) {
    auto* ctx = static_cast<FileContext*>(fileContext);
    if (ctx) {
        delete ctx;
    }
}

NTSTATUS FileCallbacks::read(FSP_FILE_SYSTEM* fs, PVOID fileContext, UINT64 buffer,
                              UINT64 offset, UINT32 length, PUINT32 bytesTransferred) {
    auto* ctx = static_cast<FileContext*>(fileContext);
    if (!ctx) return STATUS_INVALID_PARAMETER;

    if (length == 0) {
        *bytesTransferred = 0;
        return STATUS_END_OF_FILE;
    }

    // 先尝试从内存缓存读取
    auto& cache = MemoryCache::instance();
    std::vector<uint8_t> tempBuffer(length);
    int cachedBytes = cache.read(ctx->remotePath, offset, tempBuffer.data(), length);

    if (cachedBytes == static_cast<int>(length)) {
        // 全部命中缓存
        auto* dst = reinterpret_cast<void*>(static_cast<uintptr_t>(buffer));
        memcpy(dst, tempBuffer.data(), length);
        *bytesTransferred = length;
        return STATUS_SUCCESS;
    }

    // 部分或全部未命中，从 SMB 读取
    auto borrower = ConnectionPool::instance().borrow();
    if (!borrower) return STATUS_DEVICE_DOES_NOT_EXIST;

    // 从未命中的偏移开始读取
    uint64_t readOffset = offset + cachedBytes;
    uint32_t readLength = length - cachedBytes;

    int64_t smbBytesRead = borrower->readFile(ctx->remotePath, readOffset,
        tempBuffer.data() + cachedBytes, readLength);

    if (smbBytesRead < 0) {
        *bytesTransferred = 0;
        return STATUS_IO_DEVICE_ERROR;
    }

    uint32_t totalBytes = static_cast<uint32_t>(cachedBytes + smbBytesRead);

    // 将 SMB 读取的数据写入缓存
    if (smbBytesRead > 0) {
        cache.put(ctx->remotePath, readOffset, tempBuffer.data() + cachedBytes,
                  static_cast<uint32_t>(smbBytesRead));
    }

    // 复制到 WinFsp 缓冲区
    auto* dst = reinterpret_cast<void*>(static_cast<uintptr_t>(buffer));
    memcpy(dst, tempBuffer.data(), totalBytes);

    *bytesTransferred = totalBytes;

    if (totalBytes == 0) {
        return STATUS_END_OF_FILE;
    }

    return STATUS_SUCCESS;
}

NTSTATUS FileCallbacks::write(FSP_FILE_SYSTEM* fs, PVOID fileContext, UINT64 buffer,
                               UINT64 offset, UINT32 length, BOOLEAN writeToEndOfFile,
                               BOOLEAN constrainedIo, PUINT32 bytesTransferred,
                               FSP_FSCTL_FILE_INFO* fileInfo) {
    auto* ctx = static_cast<FileContext*>(fileContext);
    if (!ctx) return STATUS_INVALID_PARAMETER;

    // 使缓存失效
    MemoryCache::instance().invalidate(ctx->remotePath);

    auto* src = reinterpret_cast<const void*>(static_cast<uintptr_t>(buffer));

    uint64_t writeOffset = writeToEndOfFile ? ctx->fileSize : offset;

    auto borrower = ConnectionPool::instance().borrow();
    if (!borrower) return STATUS_DEVICE_DOES_NOT_EXIST;

    int64_t written = borrower->writeFile(ctx->remotePath, writeOffset, src, length);
    if (written < 0) {
        *bytesTransferred = 0;
        return STATUS_IO_DEVICE_ERROR;
    }

    *bytesTransferred = static_cast<uint32_t>(written);

    // 更新文件大小
    if (writeOffset + written > ctx->fileSize) {
        ctx->fileSize = writeOffset + written;
    }

    // 获取最新文件信息
    RemoteFileInfo info;
    if (borrower->getFileInfo(ctx->remotePath, info)) {
        fillFileInfo(info, fileInfo);
    }

    return STATUS_SUCCESS;
}

NTSTATUS FileCallbacks::flush(FSP_FILE_SYSTEM* fs, PVOID fileContext,
                               FSP_FSCTL_FILE_INFO* fileInfo) {
    // SMB 写入是同步的，无需额外刷新
    if (fileContext && fileInfo) {
        return getFileInfo(fs, fileContext, fileInfo);
    }
    return STATUS_SUCCESS;
}

NTSTATUS FileCallbacks::getFileInfo(FSP_FILE_SYSTEM* fs, PVOID fileContext,
                                     FSP_FSCTL_FILE_INFO* fileInfo) {
    auto* ctx = static_cast<FileContext*>(fileContext);
    if (!ctx) return STATUS_INVALID_PARAMETER;

    auto borrower = ConnectionPool::instance().borrow();
    if (!borrower) return STATUS_DEVICE_DOES_NOT_EXIST;

    RemoteFileInfo info;
    if (!borrower->getFileInfo(ctx->remotePath, info)) {
        // 使用上下文中的缓存信息
        auto timeToWinFileTime = [](time_t t) -> UINT64 {
            if (t == 0) return 0;
            return static_cast<UINT64>(t) * 10000000 + 116444736000000000ULL;
        };
        fileInfo->FileAttributes = ctx->attributes;
        fileInfo->ReparseTag = 0;
        fileInfo->AllocationSize = ((ctx->fileSize + 4095) / 4096) * 4096;
        fileInfo->FileSize = ctx->fileSize;
        fileInfo->CreationTime = timeToWinFileTime(ctx->createTime);
        fileInfo->LastAccessTime = timeToWinFileTime(ctx->accessTime);
        fileInfo->LastWriteTime = timeToWinFileTime(ctx->modifyTime);
        fileInfo->ChangeTime = fileInfo->LastWriteTime;
        fileInfo->IndexNumber = 0;
        return STATUS_SUCCESS;
    }

    ctx->fileSize = info.size;
    ctx->attributes = info.attributes;
    ctx->createTime = info.createTime;
    ctx->modifyTime = info.modifyTime;
    ctx->accessTime = info.accessTime;

    fillFileInfo(info, fileInfo);
    return STATUS_SUCCESS;
}

NTSTATUS FileCallbacks::setFileInfo(FSP_FILE_SYSTEM* fs, PVOID fileContext,
                                     UINT32 fileAttributes, UINT64 creationTime,
                                     UINT64 lastAccessTime, UINT64 lastWriteTime,
                                     UINT64 changeTime, FSP_FSCTL_FILE_INFO* fileInfo) {
    // 简化处理：SMB 文件属性设置有限支持
    return getFileInfo(fs, fileContext, fileInfo);
}

NTSTATUS FileCallbacks::canDelete(FSP_FILE_SYSTEM* fs, PVOID fileContext,
                                   const char* fileName) {
    auto* ctx = static_cast<FileContext*>(fileContext);
    if (!ctx) return STATUS_INVALID_PARAMETER;

    // 检查目录是否为空
    if (ctx->isDirectory) {
        auto borrower = ConnectionPool::instance().borrow();
        if (!borrower) return STATUS_DEVICE_DOES_NOT_EXIST;

        std::vector<RemoteFileInfo> entries;
        if (borrower->listDirectory(ctx->remotePath, entries)) {
            if (!entries.empty()) {
                return STATUS_DIRECTORY_NOT_EMPTY;
            }
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS FileCallbacks::rename(FSP_FILE_SYSTEM* fs, PVOID fileContext,
                                const char* fileName, const char* newFileName,
                                BOOLEAN replaceIfExists) {
    auto* ctx = static_cast<FileContext*>(fileContext);
    if (!ctx) return STATUS_INVALID_PARAMETER;

    std::string newVPath = normalizePath(newFileName);
    std::string newSmbPath = toSmbPath(newVPath);

    auto borrower = ConnectionPool::instance().borrow();
    if (!borrower) return STATUS_DEVICE_DOES_NOT_EXIST;

    if (!borrower->rename(ctx->remotePath, newSmbPath)) {
        return STATUS_ACCESS_DENIED;
    }

    // 使旧路径缓存失效
    MemoryCache::instance().invalidate(ctx->remotePath);

    ctx->remotePath = newSmbPath;
    ctx->fileName = newVPath;
    return STATUS_SUCCESS;
}

NTSTATUS FileCallbacks::readDirectory(FSP_FILE_SYSTEM* fs, PVOID fileContext,
                                       const char* pattern, const char* marker,
                                       UINT64 buffer, UINT32 length,
                                       PUINT32 bytesTransferred) {
    auto* ctx = static_cast<FileContext*>(fileContext);
    if (!ctx) return STATUS_INVALID_PARAMETER;

    // 检查目录缓存
    auto& cache = MemoryCache::instance();
    const auto& diskConfig = Config::instance().diskConfig();

    std::vector<std::string> cachedEntries;
    std::vector<RemoteFileInfo> entries;

    // 从 SMB 枚举目录
    auto borrower = ConnectionPool::instance().borrow();
    if (!borrower) return STATUS_DEVICE_DOES_NOT_EXIST;

    if (!borrower->listDirectory(ctx->remotePath, entries)) {
        *bytesTransferred = 0;
        return STATUS_SUCCESS;
    }

    // 构建 WinFsp 目录信息
    auto* dirInfo = reinterpret_cast<FSP_FSCTL_DIR_INFO*>(reinterpret_cast<void*>(static_cast<uintptr_t>(buffer)));
    UINT32 bytesWritten = 0;
    std::string markerStr = marker ? marker : "";

    for (const auto& entry : entries) {
        // marker 处理：跳过 marker 之前的条目
        if (!markerStr.empty() && entry.name < markerStr) {
            continue;
        }

        std::wstring wName(entry.name.begin(), entry.name.end());
        UINT16 nameLen = static_cast<UINT16>(wName.size() * sizeof(WCHAR));
        UINT32 entrySize = static_cast<UINT32>(sizeof(FSP_FSCTL_DIR_INFO) + nameLen);

        // 检查缓冲区是否足够
        if (bytesWritten + entrySize > length) {
            break;
        }

        // 填充目录条目
        memset(dirInfo, 0, sizeof(FSP_FSCTL_DIR_INFO));
        fillFileInfo(entry, &dirInfo->FileInfo);
        dirInfo->Size = entrySize;
        memcpy(dirInfo->FileNameBuf, wName.data(), nameLen);

        dirInfo = reinterpret_cast<FSP_FSCTL_DIR_INFO*>(
            reinterpret_cast<uint8_t*>(dirInfo) + entrySize);
        bytesWritten += entrySize;
    }

    *bytesTransferred = bytesWritten;
    return STATUS_SUCCESS;
}

NTSTATUS FileCallbacks::setDelete(FSP_FILE_SYSTEM* fs, PVOID fileContext,
                                   const char* fileName, BOOLEAN deleteFile) {
    // 标记删除在 cleanup 中处理
    return STATUS_SUCCESS;
}

#endif // _WIN32

} // namespace rgh
