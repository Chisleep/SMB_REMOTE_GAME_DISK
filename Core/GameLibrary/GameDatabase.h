#pragma once

#include "Common/Types.h"
#include <string>
#include <vector>
#include <optional>
#include <mutex>

#ifdef _WIN32
#include "sqlite3.h"
#else
typedef struct sqlite3 sqlite3;
#endif

namespace rgh {

// 游戏数据库（SQLite）
// 存储游戏元数据、游玩记录、标签等
class GameDatabase {
public:
    static GameDatabase& instance();

    // 初始化数据库，dbPath 为 SQLite 文件路径
    bool init(const std::string& dbPath);
    void close();

    // ---- 游戏管理 ----
    GameId addGame(const GameInfo& game);
    bool removeGame(GameId id);
    bool updateGame(const GameInfo& game);
    std::optional<GameInfo> getGame(GameId id);
    std::vector<GameInfo> getAllGames();
    std::vector<GameInfo> getGamesByTag(const std::string& tag);

    // ---- 标签管理 ----
    std::vector<std::string> getAllTags();
    bool addTagToGame(GameId gameId, const std::string& tag);
    bool removeTagFromGame(GameId gameId, const std::string& tag);

    // ---- 游玩记录 ----
    int64_t addPlayRecord(const PlayRecord& record);
    std::vector<PlayRecord> getPlayRecords(GameId gameId, int limit = 50);
    std::vector<PlayRecord> getRecentRecords(int limit = 20);

    // ---- 统计查询 ----
    int getGamePlayCount(GameId gameId);
    int64_t getGamePlayTime(GameId gameId);
    int getTotalPlayCount();
    int64_t getTotalPlayTime();

    // 排行榜：按游玩时长或次数
    struct GameStat {
        GameId gameId;
        std::string gameName;
        int playCount;
        int64_t totalPlayTimeSec;
        time_t lastPlayed;
    };
    std::vector<GameStat> getTopGamesByPlayTime(int limit = 10);
    std::vector<GameStat> getTopGamesByPlayCount(int limit = 10);

private:
    GameDatabase() = default;

    bool createTables();
    GameInfo parseGameFromStmt(void* stmt);

    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};

} // namespace rgh
