#pragma once

#include "Common/Types.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <ctime>

namespace rgh {

// 统计记录器
// 跟踪游戏启动/退出时间，记录游玩时长到数据库
class StatsRecorder {
public:
    static StatsRecorder& instance();

    // 开始一次游玩会话
    void startSession(const std::string& sessionId, GameId gameId);

    // 结束游玩会话，记录到数据库
    void endSession(const std::string& sessionId, bool normalExit);

    // 获取当前会话信息
    struct SessionInfo {
        GameId gameId;
        time_t startTime;
        bool active;
    };

    SessionInfo getSession(const std::string& sessionId);

    // 获取总统计
    int getTotalPlayCount();
    int64_t getTotalPlayTime();

    // 格式化时长为可读字符串
    static std::string formatDuration(int64_t seconds);

private:
    StatsRecorder() = default;

    std::mutex mutex_;
    std::unordered_map<std::string, SessionInfo> sessions_;
};

} // namespace rgh
