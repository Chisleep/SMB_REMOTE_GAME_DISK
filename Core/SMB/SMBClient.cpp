#include "SMB/SMBClient.h"
#include "Common/Logger.h"

#ifdef _WIN32
#include <windows.h>
#include <winnetwk.h>
#include <lm.h>
#pragma comment(lib, "mpr.lib")
#pragma comment(lib, "netapi32.lib")
#else
#include <sys/time.h>
#endif

#include <chrono>

namespace rgh {

SMBClient::SMBClient(const SmbServerConfig& config) : config_(config) {}

SMBClient::~SMBClient() {
    disconnect();
}

std::string SMBClient::getFullPath(const std::string& relativePath) const {
    return config_.toUncPath(relativePath);
}

bool SMBClient::connect() {
#ifdef _WIN32
    if (connected_) return true;

    // 如果有用户名密码，建立网络凭据
    if (!config_.username.empty()) {
        NETRESOURCEA nr = {};
        nr.dwType = RESOURCETYPE_DISK;
        nr.lpRemoteName = const_cast<char*>(config_.toUncPath().c_str());

        std::string fullUser = config_.domain.empty()
            ? config_.username
            : (config_.domain + "\\" + config_.username);

        DWORD result = WNetAddConnection2A(&nr,
            config_.password.empty() ? nullptr : config_.password.c_str(),
            fullUser.c_str(),
            CONNECT_TEMPORARY | CONNECT_INTERACTIVE);

        if (result != NO_ERROR && result != ERROR_ALREADY_ASSIGNED) {
            RGH_LOG_ERROR("SMBClient", "WNetAddConnection2 失败，错误码: " + std::to_string(result));
            // 继续尝试，可能已有缓存凭据
        }
    }

    // 打开根目录句柄用于保活
    std::string rootPath = config_.toUncPath();
    rootHandle_ = CreateFileA(rootPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (rootHandle_ == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        RGH_LOG_ERROR("SMBClient", "无法打开SMB根目录: " + rootPath + "，错误码: " + std::to_string(err));
        connected_ = false;
        return false;
    }

    connected_ = true;
    RGH_LOG_INFO("SMBClient", "SMB连接成功: " + rootPath);
    return true;
#else
    RGH_LOG_WARN("SMBClient", "非 Windows 平台，SMB 连接不可用");
    return false;
#endif
}

void SMBClient::disconnect() {
#ifdef _WIN32
    if (rootHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(rootHandle_);
        rootHandle_ = INVALID_HANDLE_VALUE;
    }

    if (connected_) {
        // 取消网络连接
        WNetCancelConnection2A(config_.toUncPath().c_str(), 0, TRUE);
    }
#endif
    connected_ = false;
}

bool SMBClient::ensureConnected() {
    if (connected_) {
#ifdef _WIN32
        // 检查根句柄是否仍有效
        if (rootHandle_ != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info;
            if (GetFileInformationByHandle(rootHandle_, &info)) {
                return true;
            }
        }
#endif
        RGH_LOG_WARN("SMBClient", "连接已断开，尝试重连...");
        disconnect();
    }
    return connect();
}

#ifdef _WIN32
HANDLE SMBClient::openFile(const std::string& relativePath, DWORD access, DWORD share,
                            DWORD disposition, DWORD flags) {
    if (!ensureConnected()) return INVALID_HANDLE_VALUE;

    std::string fullPath = getFullPath(relativePath);
    HANDLE h = CreateFileA(fullPath.c_str(), access, share, nullptr, disposition, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        RGH_LOG_DEBUG("SMBClient", "打开文件失败: " + fullPath + "，错误码: " + std::to_string(err));
    }
    return h;
}
#endif

int64_t SMBClient::readFile(const std::string& relativePath, uint64_t offset,
                             void* buffer, uint32_t size) {
#ifdef _WIN32
    if (!ensureConnected()) return -1;

    HANDLE h = openFile(relativePath, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED);

    if (h == INVALID_HANDLE_VALUE) return -1;

    OVERLAPPED ov = {};
    ov.Offset = static_cast<DWORD>(offset);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    DWORD bytesRead = 0;
    BOOL ok = ReadFile(h, buffer, size, &bytesRead, &ov);

    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            WaitForSingleObject(ov.hEvent, INFINITE);
            GetOverlappedResult(h, &ov, &bytesRead, FALSE);
        } else {
            RGH_LOG_DEBUG("SMBClient", "读取失败: " + relativePath + " 偏移:" + std::to_string(offset));
            CloseHandle(ov.hEvent);
            CloseHandle(h);
            return -1;
        }
    }

    CloseHandle(ov.hEvent);
    CloseHandle(h);
    return static_cast<int64_t>(bytesRead);
#else
    return -1;
#endif
}

int64_t SMBClient::writeFile(const std::string& relativePath, uint64_t offset,
                              const void* buffer, uint32_t size) {
#ifdef _WIN32
    if (!ensureConnected()) return -1;

    HANDLE h = openFile(relativePath, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL);

    if (h == INVALID_HANDLE_VALUE) return -1;

    OVERLAPPED ov = {};
    ov.Offset = static_cast<DWORD>(offset);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);

    DWORD bytesWritten = 0;
    if (!WriteFile(h, buffer, size, &bytesWritten, &ov)) {
        CloseHandle(h);
        return -1;
    }

    CloseHandle(h);
    return static_cast<int64_t>(bytesWritten);
#else
    return -1;
#endif
}

bool SMBClient::getFileInfo(const std::string& relativePath, RemoteFileInfo& info) {
#ifdef _WIN32
    if (!ensureConnected()) return false;

    std::string fullPath = getFullPath(relativePath);
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(fullPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        RGH_LOG_DEBUG("SMBClient", "查询文件信息失败: " + fullPath);
        return false;
    }
    FindClose(hFind);

    info.name = findData.cFileName;
    info.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info.isReadOnly = (findData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    info.attributes = findData.dwFileAttributes;
    info.size = (static_cast<uint64_t>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;

    // FILETIME 转 time_t
    auto fileTimeToTimeT = [](const FILETIME& ft) -> time_t {
        ULARGE_INTEGER li;
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        return static_cast<time_t>((li.QuadPart - 116444736000000000ULL) / 10000000ULL);
    };
    info.createTime = fileTimeToTimeT(findData.ftCreationTime);
    info.modifyTime = fileTimeToTimeT(findData.ftLastWriteTime);
    info.accessTime = fileTimeToTimeT(findData.ftLastAccessTime);

    return true;
#else
    return false;
#endif
}

bool SMBClient::listDirectory(const std::string& relativePath, std::vector<RemoteFileInfo>& entries) {
#ifdef _WIN32
    if (!ensureConnected()) return false;

    std::string searchPath = getFullPath(relativePath);
    if (!searchPath.empty() && searchPath.back() != '\\') {
        searchPath += '\\';
    }
    searchPath += '*';

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        RGH_LOG_DEBUG("SMBClient", "枚举目录失败: " + searchPath);
        return false;
    }

    auto fileTimeToTimeT = [](const FILETIME& ft) -> time_t {
        ULARGE_INTEGER li;
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        return static_cast<time_t>((li.QuadPart - 116444736000000000ULL) / 10000000ULL);
    };

    do {
        // 跳过 . 和 ..
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
            continue;
        }

        RemoteFileInfo info;
        info.name = findData.cFileName;
        info.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        info.isReadOnly = (findData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        info.attributes = findData.dwFileAttributes;
        info.size = (static_cast<uint64_t>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
        info.createTime = fileTimeToTimeT(findData.ftCreationTime);
        info.modifyTime = fileTimeToTimeT(findData.ftLastWriteTime);
        info.accessTime = fileTimeToTimeT(findData.ftLastAccessTime);
        entries.push_back(info);
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    return true;
#else
    return false;
#endif
}

bool SMBClient::createDirectory(const std::string& relativePath) {
#ifdef _WIN32
    if (!ensureConnected()) return false;
    std::string fullPath = getFullPath(relativePath);
    return CreateDirectoryA(fullPath.c_str(), nullptr) != 0;
#else
    return false;
#endif
}

bool SMBClient::deleteFile(const std::string& relativePath) {
#ifdef _WIN32
    if (!ensureConnected()) return false;
    std::string fullPath = getFullPath(relativePath);
    return DeleteFileA(fullPath.c_str()) != 0;
#else
    return false;
#endif
}

bool SMBClient::removeDirectory(const std::string& relativePath) {
#ifdef _WIN32
    if (!ensureConnected()) return false;
    std::string fullPath = getFullPath(relativePath);
    return RemoveDirectoryA(fullPath.c_str()) != 0;
#else
    return false;
#endif
}

bool SMBClient::rename(const std::string& oldPath, const std::string& newPath) {
#ifdef _WIN32
    if (!ensureConnected()) return false;
    std::string fullOld = getFullPath(oldPath);
    std::string fullNew = getFullPath(newPath);
    return MoveFileA(fullOld.c_str(), fullNew.c_str()) != 0;
#else
    return false;
#endif
}

uint32_t SMBClient::pingLatencyMs() {
#ifdef _WIN32
    if (!ensureConnected()) return UINT32_MAX;

    auto start = std::chrono::high_resolution_clock::now();

    // 通过查询根目录属性来测量延迟
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(getFullPath("").c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        FindClose(hFind);
    }

    auto end = std::chrono::high_resolution_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
#else
    return 0;
#endif
}

} // namespace rgh
