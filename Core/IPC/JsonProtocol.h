#pragma once

#include "Common/Types.h"
#include <string>
#include <vector>
#include <map>
#include <variant>

namespace rgh {

// IPC 消息类型枚举
// GUI (C# WPF) <-> Service (C++) 通过命名管道通信
enum class IpcAction {
    // 连接管理
    Ping,
    Pong,
    GetStatus,
    StatusResponse,

    // SMB 配置
    TestSmbConnection,
    SmbConnectionResult,
    SaveConfig,
    LoadConfig,
    ConfigResponse,

    // 游戏库管理
    ScanGames,
    ScanProgress,
    ScanComplete,
    AddGame,
    RemoveGame,
    UpdateGame,
    GetGameList,
    GameListResponse,
    GetGameInfo,
    GameInfoResponse,

    // 游戏启动
    LaunchGame,
    LaunchProgress,
    LaunchComplete,
    LaunchFailed,
    TerminateGame,
    GameExited,

    // 统计
    GetStats,
    StatsResponse,
    GetPlayRecords,
    PlayRecordsResponse,

    // 虚拟磁盘
    GetDiskStats,
    DiskStatsResponse,

    // 错误
    Error
};

// IPC 消息（JSON 格式传输）
struct IpcRequest {
    IpcAction action;
    std::string sessionId;
    std::map<std::string, std::string> params;
    std::string jsonData; // 复杂数据用 JSON 字符串
};

struct IpcResponse {
    IpcAction action;
    std::string sessionId;
    bool success = true;
    std::string error;
    std::map<std::string, std::string> params;
    std::string jsonData;
};

// JSON 序列化/反序列化（简化实现，实际可用 nlohmann/json）
class JsonProtocol {
public:
    // 请求/响应 <-> JSON 字符串
    static std::string serializeRequest(const IpcRequest& req);
    static std::string serializeResponse(const IpcResponse& resp);

    static IpcRequest parseRequest(const std::string& json);
    static IpcResponse parseResponse(const std::string& json);

    // action 枚举转换
    static const char* actionToString(IpcAction action);
    static IpcAction stringToAction(const std::string& str);

    // 辅助：生成唯一 sessionId
    static std::string generateSessionId();

    // 辅助：转义/反转义 JSON 字符串
    static std::string escapeJson(const std::string& s);
    static std::string unescapeJson(const std::string& s);
};

} // namespace rgh
