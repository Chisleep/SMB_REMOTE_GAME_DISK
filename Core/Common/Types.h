#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>
#include <optional>

namespace rgh {

// ===== 基础类型别名 =====
using GameId = int64_t;
using SessionId = std::string;

// ===== SMB 服务器配置 =====
struct SmbServerConfig {
    std::string host;           // 如 "192.168.5.103"
    uint16_t port = 445;        // SMB 端口
    std::string shareName;      // 共享名，如 "Games"
    std::string username;
    std::string password;
    std::string domain;         // 通常为工作组或机器名

    // 拼接 UNC 路径，如 \\192.168.5.103\Games
    std::string toUncPath() const {
        return "\\\\" + host + "\\" + shareName;
    }

    // 拼接完整 UNC 子路径
    std::string toUncPath(const std::string& subPath) const {
        std::string base = toUncPath();
        if (!subPath.empty()) {
            base += "\\" + subPath;
        }
        return base;
    }
};

// ===== 虚拟磁盘配置 =====
struct VirtualDiskConfig {
    char driveLetter = 'G';         // 盘符
    size_t memoryCacheSizeMB = 512; // 内存缓存上限(MB)，0=禁用
    bool enableReadAhead = true;    // 预读优化
    size_t readAheadSizeKB = 1024;  // 预读块大小
    uint32_t dirCacheTtlSec = 30;   // 目录元数据缓存TTL
};

// ===== 游戏信息 =====
enum class GameCompatibility {
    Unknown = 0,
    Perfect,       // 完美运行
    Experimental,  // 实验性，可能有问题
    Incompatible   // 不兼容（如反作弊拦截）
};

struct GameInfo {
    GameId id = 0;
    std::string name;              // 游戏名
    std::string smbPath;           // SMB上的相对路径，如 "ActionGame"
    std::string exeRelativePath;   // 相对exe路径，如 "ActionGame/game.exe"
    std::string launchArgs;        // 启动参数
    std::string coverImagePath;    // 封面图本地缓存路径
    std::vector<std::string> tags; // 标签
    GameCompatibility compatibility = GameCompatibility::Unknown;

    // 统计数据
    int playCount = 0;
    int64_t totalPlayTimeSec = 0;  // 总游玩时长
    time_t lastPlayed = 0;
    time_t addedDate = 0;
};

// ===== 游玩记录 =====
struct PlayRecord {
    int64_t id = 0;
    GameId gameId = 0;
    time_t startTime = 0;
    time_t endTime = 0;
    int64_t durationSec = 0;
    bool normalExit = false;
};

// ===== 启动状态 =====
enum class LaunchStatus {
    Idle,
    CheckingConnection,
    MountingDisk,
    PreparingEnvironment,
    Launching,
    Running,
    Exiting,
    Completed,
    Failed
};

// ===== IPC 消息基类 =====
struct IpcMessage {
    std::string action;
    std::string sessionId;
};

} // namespace rgh
