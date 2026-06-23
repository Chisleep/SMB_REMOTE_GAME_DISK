#pragma once

#include "Common/Types.h"
#include "SMB/SMBClient.h"

#ifdef _WIN32
#include <winfsp/winfsp.h>
#endif

#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace rgh {

// 打开的文件上下文
// 每个打开的文件对应一个 FileContext，保存 SMB 句柄和文件信息
struct FileContext {
    std::string remotePath;      // SMB 上的完整相对路径
    std::string fileName;        // 文件名
    uint64_t fileSize = 0;       // 文件大小
    bool isDirectory = false;    // 是否目录
    uint32_t attributes = 0;     // 文件属性
    time_t createTime = 0;
    time_t modifyTime = 0;
    time_t accessTime = 0;

#ifdef _WIN32
    HANDLE smbHandle = INVALID_HANDLE_VALUE; // SMB 文件句柄
#endif

    // 目录枚举状态
    bool dirEnumerated = false;
    std::vector<std::string> dirEntries;

    ~FileContext() {
#ifdef _WIN32
        if (smbHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(smbHandle);
        }
#endif
    }
};

// WinFsp 文件操作回调实现
// 这些函数被 WinFsp 调用，将文件操作转发到 SMB 后端
class FileCallbacks {
public:
    // 设置当前挂载的 SMB 根路径
    static void setSmbRootPath(const std::string& path) { smbRootPath_ = path; }

#ifdef _WIN32
    // ---- WinFsp 回调函数 ----

    static NTSTATUS getVolumeInfo(FSP_FILE_SYSTEM* fs,
                                   FSP_FSCTL_VOLUME_INFO* volumeInfo);

    static NTSTATUS setVolumeLabel(FSP_FILE_SYSTEM* fs,
                                    const char* volumeLabel,
                                    FSP_FSCTL_VOLUME_INFO* volumeInfo);

    static NTSTATUS getSecurityByName(FSP_FILE_SYSTEM* fs,
                                       const char* fileName,
                                       PUINT32 fileAttributes,
                                       PSECURITY_DESCRIPTOR securityDescriptor,
                                       size_t* securityDescriptorSize);

    static NTSTATUS create(FSP_FILE_SYSTEM* fs,
                           const char* fileName,
                           UINT32 createOptions,
                           UINT32 grantedAccess,
                           UINT32 fileAttributes,
                           PSECURITY_DESCRIPTOR securityDescriptor,
                           UINT64 allocationSize,
                           PVOID* fileContext,
                           FSP_FSCTL_FILE_INFO* fileInfo);

    static NTSTATUS open(FSP_FILE_SYSTEM* fs,
                         const char* fileName,
                         UINT32 createOptions,
                         UINT32 grantedAccess,
                         PVOID* fileContext,
                         FSP_FSCTL_FILE_INFO* fileInfo);

    static NTSTATUS overwrite(FSP_FILE_SYSTEM* fs,
                              PVOID fileContext,
                              UINT32 fileAttributes,
                              BOOLEAN replaceFileAttributes,
                              UINT64 allocationSize,
                              FSP_FSCTL_FILE_INFO* fileInfo);

    static void cleanup(FSP_FILE_SYSTEM* fs,
                        PVOID fileContext,
                        const char* fileName,
                        BOOLEAN deleteFile);

    static void close(FSP_FILE_SYSTEM* fs, PVOID fileContext);

    static NTSTATUS read(FSP_FILE_SYSTEM* fs,
                         PVOID fileContext,
                         UINT64 buffer,
                         UINT64 offset,
                         UINT32 length,
                         PUINT32 bytesTransferred);

    static NTSTATUS write(FSP_FILE_SYSTEM* fs,
                          PVOID fileContext,
                          UINT64 buffer,
                          UINT64 offset,
                          UINT32 length,
                          BOOLEAN writeToEndOfFile,
                          BOOLEAN constrainedIo,
                          PUINT32 bytesTransferred,
                          FSP_FSCTL_FILE_INFO* fileInfo);

    static NTSTATUS flush(FSP_FILE_SYSTEM* fs,
                          PVOID fileContext,
                          FSP_FSCTL_FILE_INFO* fileInfo);

    static NTSTATUS getFileInfo(FSP_FILE_SYSTEM* fs,
                                PVOID fileContext,
                                FSP_FSCTL_FILE_INFO* fileInfo);

    static NTSTATUS setFileInfo(FSP_FILE_SYSTEM* fs,
                                PVOID fileContext,
                                UINT32 fileAttributes,
                                UINT64 creationTime,
                                UINT64 lastAccessTime,
                                UINT64 lastWriteTime,
                                UINT64 changeTime,
                                FSP_FSCTL_FILE_INFO* fileInfo);

    static NTSTATUS canDelete(FSP_FILE_SYSTEM* fs,
                              PVOID fileContext,
                              const char* fileName);

    static NTSTATUS rename(FSP_FILE_SYSTEM* fs,
                           PVOID fileContext,
                           const char* fileName,
                           const char* newFileName,
                           BOOLEAN replaceIfExists);

    static NTSTATUS readDirectory(FSP_FILE_SYSTEM* fs,
                                  PVOID fileContext,
                                  const char* pattern,
                                  const char* marker,
                                  UINT64 buffer,
                                  UINT32 length,
                                  PUINT32 bytesTransferred);

    static NTSTATUS setDelete(FSP_FILE_SYSTEM* fs,
                              PVOID fileContext,
                              const char* fileName,
                              BOOLEAN deleteFile);

#endif

    // 辅助函数
    static std::string normalizePath(const char* fileName);
    static std::string toSmbPath(const std::string& virtualPath);
    static void fillFileInfo(const RemoteFileInfo& remote, FSP_FSCTL_FILE_INFO* fileInfo);

private:
    static std::string smbRootPath_; // 当前挂载的 SMB 根路径
};

} // namespace rgh
