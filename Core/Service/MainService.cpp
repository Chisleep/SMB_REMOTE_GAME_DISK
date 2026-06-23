#include "Service/MainService.h"
#include "Common/Logger.h"
#include "Common/Config.h"
#include "Common/CredentialStore.h"
#include "SMB/ConnectionPool.h"
#include "SMB/SMBClient.h"
#include "VirtualDisk/MemoryCache.h"
#include "GameLibrary/GameDatabase.h"
#include "GameLibrary/GameScanner.h"
#include "GameLibrary/CoverFetcher.h"
#include "Launcher/LaunchManager.h"
#include "Stats/StatsRecorder.h"
#include "IPC/JsonProtocol.h"

#include <sstream>
#include <chrono>

namespace rgh {

MainService& MainService::instance() {
    static MainService svc;
    return svc;
}

bool MainService::init() {
    if (initialized_.load()) return true;

    RGH_LOG_INFO("MainService", "=== RemoteGameHub 服务初始化 ===");

    if (!initLogger()) return false;
    if (!initConfig()) return false;
    if (!initDatabase()) return false;
    if (!initSmbConnection()) return false;
    if (!initVirtualDisk()) return false;
    if (!initIpcServer()) return false;

    // 初始化启动管理器
    LaunchManager::instance().setVirtualDiskEngine(vdEngine_.get());

    initialized_ = true;
    RGH_LOG_INFO("MainService", "=== 服务初始化完成 ===");
    return true;
}

bool MainService::initLogger() {
    Logger::instance().init(Config::instance().logDir(), LogLevel::Debug);
    RGH_LOG_INFO("MainService", "日志系统已初始化");
    return true;
}

bool MainService::initConfig() {
    Config::instance().load();
    return true;
}

bool MainService::initDatabase() {
    std::string dbPath = Config::instance().dataDir() + "\\games.db";
    return GameDatabase::instance().init(dbPath);
}

bool MainService::initSmbConnection() {
    const auto& smbConfig = Config::instance().smbConfig();

    // 尝试从凭据管理器加载密码
    std::string username = smbConfig.username;
    std::string password = smbConfig.password;
    std::string domain = smbConfig.domain;

    if (password.empty()) {
        CredentialStore::instance().loadCredential(smbConfig.host, smbConfig.port,
            username, password, domain);
    }

    SmbServerConfig config = smbConfig;
    config.username = username;
    config.password = password;
    config.domain = domain;

    ConnectionPool::instance().init(config, 4);
    return ConnectionPool::instance().totalConnections() > 0;
}

bool MainService::initVirtualDisk() {
    // 初始化内存缓存
    MemoryCache::instance().init(Config::instance().diskConfig().memoryCacheSizeMB);

    // 创建虚拟磁盘引擎（不立即挂载，启动游戏时才挂载）
    vdEngine_ = std::make_unique<VirtualDiskEngine>();
    return true;
}

bool MainService::initIpcServer() {
    PipeServer::instance().setRequestHandler(
        [this](const IpcRequest& req) { return handleRequest(req); });
    return PipeServer::instance().start();
}

void MainService::run() {
    if (!initialized_.load()) {
        RGH_LOG_ERROR("MainService", "服务未初始化");
        return;
    }

    running_ = true;
    RGH_LOG_INFO("MainService", "服务已启动，等待 GUI 请求...");

    // 服务主循环（等待停止信号）
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 定期检查 SMB 连接健康
        // TODO: 健康检查逻辑
    }

    RGH_LOG_INFO("MainService", "服务正在停止...");
}

void MainService::stop() {
    running_ = false;

    // 终止运行中的游戏
    if (LaunchManager::instance().isGameRunning()) {
        LaunchManager::instance().terminateGame();
    }

    // 卸载虚拟盘
    if (vdEngine_ && vdEngine_->isMounted()) {
        vdEngine_->unmount();
    }

    // 停止 IPC 服务器
    PipeServer::instance().stop();

    // 关闭 SMB 连接
    ConnectionPool::instance().shutdown();

    // 关闭数据库
    GameDatabase::instance().close();

    RGH_LOG_INFO("MainService", "服务已停止");
}

MainService::ServiceStatus MainService::getStatus() {
    ServiceStatus status;

    auto borrower = ConnectionPool::instance().borrow();
    if (borrower) {
        status.smbConnected = borrower->isConnected();
        status.smbLatencyMs = borrower->pingLatencyMs();
    }

    if (vdEngine_) {
        status.diskMounted = vdEngine_->isMounted();
        status.mountPoint = vdEngine_->mountPoint();
    }

    status.gameRunning = LaunchManager::instance().isGameRunning();
    status.connectedClients = PipeServer::instance().connectedClients();

    return status;
}

// ===== IPC 请求处理 =====

IpcResponse MainService::handleRequest(const IpcRequest& req) {
    RGH_LOG_DEBUG("MainService", "处理请求: " + std::string(JsonProtocol::actionToString(req.action)));

    switch (req.action) {
        case IpcAction::Ping:               return handlePing(req);
        case IpcAction::GetStatus:          return handleGetStatus(req);
        case IpcAction::TestSmbConnection:  return handleTestSmb(req);
        case IpcAction::SaveConfig:         return handleSaveConfig(req);
        case IpcAction::LoadConfig:         return handleLoadConfig(req);
        case IpcAction::ScanGames:          return handleScanGames(req);
        case IpcAction::AddGame:            return handleAddGame(req);
        case IpcAction::RemoveGame:         return handleRemoveGame(req);
        case IpcAction::UpdateGame:         return handleUpdateGame(req);
        case IpcAction::GetGameList:        return handleGetGameList(req);
        case IpcAction::LaunchGame:         return handleLaunchGame(req);
        case IpcAction::TerminateGame:      return handleTerminateGame(req);
        case IpcAction::GetStats:           return handleGetStats(req);
        case IpcAction::GetDiskStats:       return handleGetDiskStats(req);
        default:
            IpcResponse resp;
            resp.action = IpcAction::Error;
            resp.success = false;
            resp.error = "未知的 action";
            resp.sessionId = req.sessionId;
            return resp;
    }
}

IpcResponse MainService::handlePing(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::Pong;
    resp.sessionId = req.sessionId;
    resp.params["time"] = std::to_string(time(nullptr));
    return resp;
}

IpcResponse MainService::handleGetStatus(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::StatusResponse;
    resp.sessionId = req.sessionId;

    auto status = getStatus();
    resp.params["smbConnected"] = status.smbConnected ? "true" : "false";
    resp.params["smbLatencyMs"] = std::to_string(status.smbLatencyMs);
    resp.params["diskMounted"] = status.diskMounted ? "true" : "false";
    resp.params["mountPoint"] = status.mountPoint;
    resp.params["gameRunning"] = status.gameRunning ? "true" : "false";
    resp.params["connectedClients"] = std::to_string(status.connectedClients);

    return resp;
}

IpcResponse MainService::handleTestSmb(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::SmbConnectionResult;
    resp.sessionId = req.sessionId;

    auto borrower = ConnectionPool::instance().borrow();
    if (borrower && borrower->isConnected()) {
        uint32_t latency = borrower->pingLatencyMs();
        resp.success = true;
        resp.params["latencyMs"] = std::to_string(latency);
        resp.params["message"] = "连接成功";
    } else {
        resp.success = false;
        resp.error = "无法连接到 SMB 服务器";
    }

    return resp;
}

IpcResponse MainService::handleSaveConfig(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::ConfigResponse;
    resp.sessionId = req.sessionId;

    // 从 params 更新配置
    auto& config = Config::instance();
    auto& smb = const_cast<SmbServerConfig&>(config.smbConfig());

    if (req.params.count("host")) smb.host = req.params.at("host");
    if (req.params.count("port")) smb.port = static_cast<uint16_t>(std::stoi(req.params.at("port")));
    if (req.params.count("shareName")) smb.shareName = req.params.at("shareName");
    if (req.params.count("username")) smb.username = req.params.at("username");
    if (req.params.count("password")) smb.password = req.params.at("password");
    if (req.params.count("domain")) smb.domain = req.params.at("domain");

    resp.success = config.save();
    if (!resp.success) {
        resp.error = "保存配置失败";
    }

    return resp;
}

IpcResponse MainService::handleLoadConfig(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::ConfigResponse;
    resp.sessionId = req.sessionId;

    const auto& config = Config::instance();
    const auto& smb = config.smbConfig();
    const auto& disk = config.diskConfig();

    resp.params["host"] = smb.host;
    resp.params["port"] = std::to_string(smb.port);
    resp.params["shareName"] = smb.shareName;
    resp.params["username"] = smb.username;
    resp.params["domain"] = smb.domain;
    resp.params["driveLetter"] = std::string(1, disk.driveLetter);
    resp.params["memoryCacheSizeMB"] = std::to_string(disk.memoryCacheSizeMB);

    resp.success = true;
    return resp;
}

IpcResponse MainService::handleScanGames(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::ScanComplete;
    resp.sessionId = req.sessionId;

    std::string rootPath = req.params.count("path") ? req.params.at("path") : "";

    auto borrower = ConnectionPool::instance().borrow();
    if (!borrower) {
        resp.success = false;
        resp.error = "SMB 连接不可用";
        return resp;
    }

    GameScanner scanner;
    scanner.setSmbClient(std::make_shared<SMBClient>(*borrower));

    auto results = scanner.scanDirectory(rootPath, [&](int current, int total, const std::string& name) {
        // 推送扫描进度事件
        IpcResponse event;
        event.action = IpcAction::ScanProgress;
        event.params["current"] = std::to_string(current);
        event.params["total"] = std::to_string(total);
        event.params["currentDir"] = name;
        PipeServer::instance().broadcastEvent(event);
    });

    // 构建 JSON 结果
    std::ostringstream json;
    json << "[";
    for (size_t i = 0; i < results.size(); ++i) {
        if (i > 0) json << ",";
        json << "{\"name\":\"" << JsonProtocol::escapeJson(results[i].name) << "\""
             << ",\"smbPath\":\"" << JsonProtocol::escapeJson(results[i].smbPath) << "\""
             << ",\"exeRelativePath\":\"" << JsonProtocol::escapeJson(results[i].exeRelativePath) << "\""
             << ",\"fileCount\":" << results[i].fileCount
             << ",\"totalSize\":" << results[i].totalSize
             << "}";
    }
    json << "]";
    resp.jsonData = json.str();
    resp.success = true;

    return resp;
}

IpcResponse MainService::handleAddGame(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::GameInfoResponse;
    resp.sessionId = req.sessionId;

    GameInfo game;
    game.name = req.params.count("name") ? req.params.at("name") : "";
    game.smbPath = req.params.count("smbPath") ? req.params.at("smbPath") : "";
    game.exeRelativePath = req.params.count("exeRelativePath") ? req.params.at("exeRelativePath") : "";
    game.launchArgs = req.params.count("launchArgs") ? req.params.at("launchArgs") : "";
    game.addedDate = time(nullptr);

    // 尝试获取封面
    if (!game.name.empty()) {
        game.coverImagePath = CoverFetcher::instance().fetchCover(game.name);
    }

    GameId id = GameDatabase::instance().addGame(game);
    if (id > 0) {
        resp.success = true;
        resp.params["gameId"] = std::to_string(id);
    } else {
        resp.success = false;
        resp.error = "添加游戏失败";
    }

    return resp;
}

IpcResponse MainService::handleRemoveGame(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::GameInfoResponse;
    resp.sessionId = req.sessionId;

    GameId id = req.params.count("gameId") ? std::stoll(req.params.at("gameId")) : 0;
    resp.success = GameDatabase::instance().removeGame(id);

    return resp;
}

IpcResponse MainService::handleUpdateGame(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::GameInfoResponse;
    resp.sessionId = req.sessionId;

    GameId id = req.params.count("gameId") ? std::stoll(req.params.at("gameId")) : 0;
    auto game = GameDatabase::instance().getGame(id);
    if (!game) {
        resp.success = false;
        resp.error = "游戏不存在";
        return resp;
    }

    if (req.params.count("name")) game->name = req.params.at("name");
    if (req.params.count("launchArgs")) game->launchArgs = req.params.at("launchArgs");
    if (req.params.count("coverImagePath")) game->coverImagePath = req.params.at("coverImagePath");

    resp.success = GameDatabase::instance().updateGame(*game);
    return resp;
}

IpcResponse MainService::handleGetGameList(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::GameListResponse;
    resp.sessionId = req.sessionId;

    auto games = GameDatabase::instance().getAllGames();

    std::ostringstream json;
    json << "[";
    for (size_t i = 0; i < games.size(); ++i) {
        if (i > 0) json << ",";
        const auto& g = games[i];
        json << "{\"id\":" << g.id
             << ",\"name\":\"" << JsonProtocol::escapeJson(g.name) << "\""
             << ",\"smbPath\":\"" << JsonProtocol::escapeJson(g.smbPath) << "\""
             << ",\"exeRelativePath\":\"" << JsonProtocol::escapeJson(g.exeRelativePath) << "\""
             << ",\"launchArgs\":\"" << JsonProtocol::escapeJson(g.launchArgs) << "\""
             << ",\"coverImagePath\":\"" << JsonProtocol::escapeJson(g.coverImagePath) << "\""
             << ",\"compatibility\":" << static_cast<int>(g.compatibility)
             << ",\"playCount\":" << g.playCount
             << ",\"totalPlayTimeSec\":" << g.totalPlayTimeSec
             << ",\"lastPlayed\":" << g.lastPlayed
             << "}";
    }
    json << "]";

    resp.jsonData = json.str();
    resp.success = true;
    return resp;
}

IpcResponse MainService::handleLaunchGame(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::LaunchComplete;
    resp.sessionId = req.sessionId;

    GameId id = req.params.count("gameId") ? std::stoll(req.params.at("gameId")) : 0;
    auto game = GameDatabase::instance().getGame(id);
    if (!game) {
        resp.success = false;
        resp.error = "游戏不存在";
        return resp;
    }

    // 设置启动状态回调（通过 IPC 推送事件）
    LaunchManager::instance().setStatusCallback(
        [](const std::string& sessionId, LaunchStatus status, const std::string& msg) {
            IpcResponse event;
            event.sessionId = sessionId;
            event.params["message"] = msg;

            switch (status) {
                case LaunchStatus::MountingDisk:
                    event.action = IpcAction::LaunchProgress;
                    event.params["status"] = "mounting";
                    break;
                case LaunchStatus::Launching:
                    event.action = IpcAction::LaunchProgress;
                    event.params["status"] = "launching";
                    break;
                case LaunchStatus::Running:
                    event.action = IpcAction::LaunchProgress;
                    event.params["status"] = "running";
                    break;
                case LaunchStatus::Completed:
                    event.action = IpcAction::LaunchComplete;
                    event.params["status"] = "completed";
                    break;
                case LaunchStatus::Failed:
                    event.action = IpcAction::LaunchFailed;
                    event.params["status"] = "failed";
                    event.success = false;
                    break;
                default:
                    event.action = IpcAction::LaunchProgress;
                    event.params["status"] = "unknown";
            }

            PipeServer::instance().broadcastEvent(event);
        });

    std::string sessionId = LaunchManager::instance().launchGame(*game);
    if (sessionId.empty()) {
        resp.success = false;
        resp.error = "启动失败（可能已有游戏在运行）";
    } else {
        resp.success = true;
        resp.sessionId = sessionId;
    }

    return resp;
}

IpcResponse MainService::handleTerminateGame(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::GameExited;
    resp.sessionId = req.sessionId;

    resp.success = LaunchManager::instance().terminateGame();
    return resp;
}

IpcResponse MainService::handleGetStats(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::StatsResponse;
    resp.sessionId = req.sessionId;

    resp.params["totalPlayCount"] = std::to_string(StatsRecorder::instance().getTotalPlayCount());
    resp.params["totalPlayTimeSec"] = std::to_string(StatsRecorder::instance().getTotalPlayTime());

    // 排行榜
    auto topByTime = GameDatabase::instance().getTopGamesByPlayTime(10);
    auto topByCount = GameDatabase::instance().getTopGamesByPlayCount(10);

    std::ostringstream json;
    json << "{\"topByPlayTime\":[";
    for (size_t i = 0; i < topByTime.size(); ++i) {
        if (i > 0) json << ",";
        json << "{\"gameId\":" << topByTime[i].gameId
             << ",\"gameName\":\"" << JsonProtocol::escapeJson(topByTime[i].gameName) << "\""
             << ",\"playCount\":" << topByTime[i].playCount
             << ",\"totalPlayTimeSec\":" << topByTime[i].totalPlayTimeSec
             << "}";
    }
    json << "],\"topByPlayCount\":[";
    for (size_t i = 0; i < topByCount.size(); ++i) {
        if (i > 0) json << ",";
        json << "{\"gameId\":" << topByCount[i].gameId
             << ",\"gameName\":\"" << JsonProtocol::escapeJson(topByCount[i].gameName) << "\""
             << ",\"playCount\":" << topByCount[i].playCount
             << ",\"totalPlayTimeSec\":" << topByCount[i].totalPlayTimeSec
             << "}";
    }
    json << "]}";

    resp.jsonData = json.str();
    resp.success = true;
    return resp;
}

IpcResponse MainService::handleGetDiskStats(const IpcRequest& req) {
    IpcResponse resp;
    resp.action = IpcAction::DiskStatsResponse;
    resp.sessionId = req.sessionId;

    if (vdEngine_) {
        const auto& stats = vdEngine_->stats();
        resp.params["totalReads"] = std::to_string(stats.totalReads.load());
        resp.params["totalWrites"] = std::to_string(stats.totalWrites.load());
        resp.params["bytesRead"] = std::to_string(stats.bytesRead.load());
        resp.params["bytesWritten"] = std::to_string(stats.bytesWritten.load());
    }

    // 内存缓存统计
    const auto& cacheStats = MemoryCache::instance().stats();
    resp.params["cacheHits"] = std::to_string(cacheStats.cacheHits.load());
    resp.params["cacheMisses"] = std::to_string(cacheStats.cacheMisses.load());
    resp.params["cacheHitRate"] = std::to_string(MemoryCache::instance().hitRate());
    resp.params["cacheBytes"] = std::to_string(cacheStats.bytesCached.load());

    resp.success = true;
    return resp;
}

} // namespace rgh
