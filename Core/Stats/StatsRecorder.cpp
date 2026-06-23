#include "Stats/StatsRecorder.h"
#include "GameLibrary/GameDatabase.h"
#include "Common/Logger.h"

#include <sstream>
#include <iomanip>

namespace rgh {

StatsRecorder& StatsRecorder::instance() {
    static StatsRecorder recorder;
    return recorder;
}

void StatsRecorder::startSession(const std::string& sessionId, GameId gameId) {
    std::lock_guard<std::mutex> lock(mutex_);

    SessionInfo info;
    info.gameId = gameId;
    info.startTime = time(nullptr);
    info.active = true;
    sessions_[sessionId] = info;

    RGH_LOG_INFO("StatsRecorder", "开始会话: " + sessionId + " 游戏ID=" + std::to_string(gameId));
}

void StatsRecorder::endSession(const std::string& sessionId, bool normalExit) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        RGH_LOG_WARN("StatsRecorder", "会话不存在: " + sessionId);
        return;
    }

    SessionInfo& info = it->second;
    info.active = false;

    time_t endTime = time(nullptr);
    int64_t duration = static_cast<int64_t>(endTime - info.startTime);

    // 最少游玩 5 秒才记录（避免误启动）
    if (duration < 5) {
        RGH_LOG_DEBUG("StatsRecorder", "会话时长过短(" + std::to_string(duration) + "s)，不记录");
        sessions_.erase(it);
        return;
    }

    // 记录到数据库
    PlayRecord record;
    record.gameId = info.gameId;
    record.startTime = info.startTime;
    record.endTime = endTime;
    record.durationSec = duration;
    record.normalExit = normalExit;

    GameDatabase::instance().addPlayRecord(record);

    RGH_LOG_INFO("StatsRecorder", "结束会话: " + sessionId +
        " 时长=" + formatDuration(duration) +
        " 正常退出=" + (normalExit ? "是" : "否"));

    sessions_.erase(it);
}

StatsRecorder::SessionInfo StatsRecorder::getSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        return it->second;
    }
    return {0, 0, false};
}

int StatsRecorder::getTotalPlayCount() {
    return GameDatabase::instance().getTotalPlayCount();
}

int64_t StatsRecorder::getTotalPlayTime() {
    return GameDatabase::instance().getTotalPlayTime();
}

std::string StatsRecorder::formatDuration(int64_t seconds) {
    int64_t hours = seconds / 3600;
    int64_t minutes = (seconds % 3600) / 60;
    int64_t secs = seconds % 60;

    std::ostringstream oss;
    if (hours > 0) {
        oss << hours << "小时" << minutes << "分";
    } else if (minutes > 0) {
        oss << minutes << "分" << secs << "秒";
    } else {
        oss << secs << "秒";
    }
    return oss.str();
}

} // namespace rgh
