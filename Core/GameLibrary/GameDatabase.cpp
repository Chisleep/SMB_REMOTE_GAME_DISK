#include "GameLibrary/GameDatabase.h"
#include "Common/Logger.h"

#ifdef _WIN32
#include "sqlite3.h"
#endif

#include <sstream>
#include <cstring>

namespace rgh {

GameDatabase& GameDatabase::instance() {
    static GameDatabase db;
    return db;
}

bool GameDatabase::init(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(mutex_);

#ifdef _WIN32
    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        RGH_LOG_ERROR("GameDatabase", "无法打开数据库: " + dbPath + " - " + sqlite3_errmsg(db_));
        return false;
    }

    // 启用 WAL 模式提升并发性能
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    if (!createTables()) {
        RGH_LOG_ERROR("GameDatabase", "创建表失败");
        return false;
    }

    RGH_LOG_INFO("GameDatabase", "数据库初始化成功: " + dbPath);
    return true;
#else
    RGH_LOG_WARN("GameDatabase", "非 Windows 平台，数据库不可用");
    return false;
#endif
}

void GameDatabase::close() {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
#endif
}

bool GameDatabase::createTables() {
#ifdef _WIN32
    const char* sql[] = {
        "CREATE TABLE IF NOT EXISTS games ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  smb_path TEXT NOT NULL,"
        "  exe_relative_path TEXT NOT NULL,"
        "  launch_args TEXT DEFAULT '',"
        "  cover_image_path TEXT DEFAULT '',"
        "  compatibility INTEGER DEFAULT 0,"
        "  play_count INTEGER DEFAULT 0,"
        "  total_play_time_sec INTEGER DEFAULT 0,"
        "  last_played INTEGER DEFAULT 0,"
        "  added_date INTEGER DEFAULT 0"
        ");",

        "CREATE TABLE IF NOT EXISTS tags ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  game_id INTEGER NOT NULL,"
        "  tag TEXT NOT NULL,"
        "  FOREIGN KEY (game_id) REFERENCES games(id) ON DELETE CASCADE,"
        "  UNIQUE(game_id, tag)"
        ");",

        "CREATE TABLE IF NOT EXISTS play_records ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  game_id INTEGER NOT NULL,"
        "  start_time INTEGER NOT NULL,"
        "  end_time INTEGER NOT NULL,"
        "  duration_sec INTEGER NOT NULL,"
        "  normal_exit INTEGER DEFAULT 0,"
        "  FOREIGN KEY (game_id) REFERENCES games(id) ON DELETE CASCADE"
        ");",

        "CREATE INDEX IF NOT EXISTS idx_tags_game_id ON tags(game_id);",
        "CREATE INDEX IF NOT EXISTS idx_records_game_id ON play_records(game_id);",
        "CREATE INDEX IF NOT EXISTS idx_records_start_time ON play_records(start_time);",
        nullptr
    };

    for (int i = 0; sql[i]; ++i) {
        char* errMsg = nullptr;
        if (sqlite3_exec(db_, sql[i], nullptr, nullptr, &errMsg) != SQLITE_OK) {
            RGH_LOG_ERROR("GameDatabase", std::string("SQL错误: ") + (errMsg ? errMsg : "unknown"));
            sqlite3_free(errMsg);
            return false;
        }
    }
    return true;
#else
    return false;
#endif
}

GameId GameDatabase::addGame(const GameInfo& game) {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "INSERT INTO games (name, smb_path, exe_relative_path, launch_args, "
                      "cover_image_path, compatibility, added_date) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        RGH_LOG_ERROR("GameDatabase", "准备语句失败: " + std::string(sqlite3_errmsg(db_)));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, game.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, game.smbPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, game.exeRelativePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, game.launchArgs.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, game.coverImagePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, static_cast<int>(game.compatibility));
    sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(game.addedDate ? game.addedDate : time(nullptr)));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        RGH_LOG_ERROR("GameDatabase", "插入游戏失败: " + std::string(sqlite3_errmsg(db_)));
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);

    GameId id = static_cast<GameId>(sqlite3_last_insert_rowid(db_));

    // 插入标签
    for (const auto& tag : game.tags) {
        addTagToGame(id, tag);
    }

    RGH_LOG_INFO("GameDatabase", "添加游戏: " + game.name + " (ID=" + std::to_string(id) + ")");
    return id;
#else
    return 0;
#endif
}

bool GameDatabase::removeGame(GameId id) {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "DELETE FROM games WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    // 标签和记录通过外键级联删除
    RGH_LOG_INFO("GameDatabase", "删除游戏 ID=" + std::to_string(id));
    return ok;
#else
    return false;
#endif
}

bool GameDatabase::updateGame(const GameInfo& game) {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "UPDATE games SET name=?, smb_path=?, exe_relative_path=?, "
                      "launch_args=?, cover_image_path=?, compatibility=? WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, game.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, game.smbPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, game.exeRelativePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, game.launchArgs.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, game.coverImagePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, static_cast<int>(game.compatibility));
    sqlite3_bind_int64(stmt, 7, game.id);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
#else
    return false;
#endif
}

GameInfo GameDatabase::parseGameFromStmt(void* stmtPtr) {
    GameInfo game;
#ifdef _WIN32
    auto* stmt = static_cast<sqlite3_stmt*>(stmtPtr);
    game.id = sqlite3_column_int64(stmt, 0);
    game.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    game.smbPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    game.exeRelativePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    game.launchArgs = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    game.coverImagePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    game.compatibility = static_cast<GameCompatibility>(sqlite3_column_int(stmt, 6));
    game.playCount = sqlite3_column_int(stmt, 7);
    game.totalPlayTimeSec = sqlite3_column_int64(stmt, 8);
    game.lastPlayed = sqlite3_column_int64(stmt, 9);
    game.addedDate = sqlite3_column_int64(stmt, 10);
#endif
    return game;
}

std::optional<GameInfo> GameDatabase::getGame(GameId id) {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "SELECT * FROM games WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        GameInfo game = parseGameFromStmt(stmt);
        sqlite3_finalize(stmt);
        return game;
    }
    sqlite3_finalize(stmt);
#endif
    return std::nullopt;
}

std::vector<GameInfo> GameDatabase::getAllGames() {
    std::vector<GameInfo> games;
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "SELECT * FROM games ORDER BY name;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        games.push_back(parseGameFromStmt(stmt));
    }
    sqlite3_finalize(stmt);
#endif
    return games;
}

std::vector<GameInfo> GameDatabase::getGamesByTag(const std::string& tag) {
    std::vector<GameInfo> games;
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "SELECT g.* FROM games g JOIN tags t ON g.id = t.game_id WHERE t.tag = ? ORDER BY g.name;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, tag.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        games.push_back(parseGameFromStmt(stmt));
    }
    sqlite3_finalize(stmt);
#endif
    return games;
}

std::vector<std::string> GameDatabase::getAllTags() {
    std::vector<std::string> tags;
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "SELECT DISTINCT tag FROM tags ORDER BY tag;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tags.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
#endif
    return tags;
}

bool GameDatabase::addTagToGame(GameId gameId, const std::string& tag) {
#ifdef _WIN32
    const char* sql = "INSERT OR IGNORE INTO tags (game_id, tag) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, gameId);
    sqlite3_bind_text(stmt, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
#else
    return false;
#endif
}

bool GameDatabase::removeTagFromGame(GameId gameId, const std::string& tag) {
#ifdef _WIN32
    const char* sql = "DELETE FROM tags WHERE game_id = ? AND tag = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, gameId);
    sqlite3_bind_text(stmt, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
#else
    return false;
#endif
}

int64_t GameDatabase::addPlayRecord(const PlayRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "INSERT INTO play_records (game_id, start_time, end_time, duration_sec, normal_exit) "
                      "VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, record.gameId);
    sqlite3_bind_int64(stmt, 2, record.startTime);
    sqlite3_bind_int64(stmt, 3, record.endTime);
    sqlite3_bind_int64(stmt, 4, record.durationSec);
    sqlite3_bind_int(stmt, 5, record.normalExit ? 1 : 0);

    int64_t id = 0;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        id = sqlite3_last_insert_rowid(db_);

        // 更新游戏统计
        const char* updateSql = "UPDATE games SET play_count = play_count + 1, "
                                "total_play_time_sec = total_play_time_sec + ?, "
                                "last_played = ? WHERE id = ?;";
        sqlite3_finalize(stmt);
        sqlite3_prepare_v2(db_, updateSql, -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, record.durationSec);
        sqlite3_bind_int64(stmt, 2, record.endTime);
        sqlite3_bind_int64(stmt, 3, record.gameId);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    return id;
#else
    return 0;
#endif
}

std::vector<PlayRecord> GameDatabase::getPlayRecords(GameId gameId, int limit) {
    std::vector<PlayRecord> records;
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "SELECT * FROM play_records WHERE game_id = ? ORDER BY start_time DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, gameId);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlayRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.gameId = sqlite3_column_int64(stmt, 1);
        r.startTime = sqlite3_column_int64(stmt, 2);
        r.endTime = sqlite3_column_int64(stmt, 3);
        r.durationSec = sqlite3_column_int64(stmt, 4);
        r.normalExit = sqlite3_column_int(stmt, 5) != 0;
        records.push_back(r);
    }
    sqlite3_finalize(stmt);
#endif
    return records;
}

std::vector<PlayRecord> GameDatabase::getRecentRecords(int limit) {
    std::vector<PlayRecord> records;
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "SELECT * FROM play_records ORDER BY start_time DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlayRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.gameId = sqlite3_column_int64(stmt, 1);
        r.startTime = sqlite3_column_int64(stmt, 2);
        r.endTime = sqlite3_column_int64(stmt, 3);
        r.durationSec = sqlite3_column_int64(stmt, 4);
        r.normalExit = sqlite3_column_int(stmt, 5) != 0;
        records.push_back(r);
    }
    sqlite3_finalize(stmt);
#endif
    return records;
}

int GameDatabase::getGamePlayCount(GameId gameId) {
#ifdef _WIN32
    const char* sql = "SELECT play_count FROM games WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, gameId);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
#else
    return 0;
#endif
}

int64_t GameDatabase::getGamePlayTime(GameId gameId) {
#ifdef _WIN32
    const char* sql = "SELECT total_play_time_sec FROM games WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, gameId);
    int64_t t = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        t = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return t;
#else
    return 0;
#endif
}

int GameDatabase::getTotalPlayCount() {
#ifdef _WIN32
    const char* sql = "SELECT COALESCE(SUM(play_count), 0) FROM games;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
#else
    return 0;
#endif
}

int64_t GameDatabase::getTotalPlayTime() {
#ifdef _WIN32
    const char* sql = "SELECT COALESCE(SUM(total_play_time_sec), 0) FROM games;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    int64_t t = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        t = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return t;
#else
    return 0;
#endif
}

std::vector<GameDatabase::GameStat> GameDatabase::getTopGamesByPlayTime(int limit) {
    std::vector<GameStat> stats;
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "SELECT id, name, play_count, total_play_time_sec, last_played "
                      "FROM games ORDER BY total_play_time_sec DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GameStat s;
        s.gameId = sqlite3_column_int64(stmt, 0);
        s.gameName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        s.playCount = sqlite3_column_int(stmt, 2);
        s.totalPlayTimeSec = sqlite3_column_int64(stmt, 3);
        s.lastPlayed = sqlite3_column_int64(stmt, 4);
        stats.push_back(s);
    }
    sqlite3_finalize(stmt);
#endif
    return stats;
}

std::vector<GameDatabase::GameStat> GameDatabase::getTopGamesByPlayCount(int limit) {
    std::vector<GameStat> stats;
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const char* sql = "SELECT id, name, play_count, total_play_time_sec, last_played "
                      "FROM games ORDER BY play_count DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GameStat s;
        s.gameId = sqlite3_column_int64(stmt, 0);
        s.gameName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        s.playCount = sqlite3_column_int(stmt, 2);
        s.totalPlayTimeSec = sqlite3_column_int64(stmt, 3);
        s.lastPlayed = sqlite3_column_int64(stmt, 4);
        stats.push_back(s);
    }
    sqlite3_finalize(stmt);
#endif
    return stats;
}

} // namespace rgh
