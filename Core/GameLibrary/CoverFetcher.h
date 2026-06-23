#pragma once

#include "Common/Types.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rgh {

// 封面获取器
// 通过游戏名在线搜索封面图，保存到本地缓存目录
// 支持 Steam 和 IGDB 数据源
class CoverFetcher {
public:
    static CoverFetcher& instance();

    // 初始化，设置封面缓存目录
    void init(const std::string& cacheDir);

    // 获取游戏封面
    // 1. 先检查本地缓存
    // 2. 尝试 Steam API（通过游戏名搜索 appid）
    // 3. 保存到缓存
    // 返回本地封面文件路径，失败返回空
    std::string fetchCover(const std::string& gameName,
                           std::function<void(int)> progress = nullptr);

    // 批量获取封面
    void fetchCovers(const std::vector<std::string>& gameNames,
                     std::function<void(int, int, const std::string&)> progress,
                     std::function<void(const std::string&, const std::string&)> onComplete);

    // 清理封面缓存
    void clearCache();

    // 封面缓存目录
    const std::string& cacheDir() const { return cacheDir_; }

private:
    CoverFetcher() = default;

    // 通过 Steam 搜索游戏
    std::string searchSteamAppId(const std::string& gameName);
    std::string downloadSteamCover(const std::string& appId, const std::string& savePath);

    // HTTP GET 请求（使用 WinHTTP）
    std::string httpGet(const std::string& url);
    bool httpDownloadFile(const std::string& url, const std::string& savePath);

    // 文件名安全化
    std::string sanitizeFileName(const std::string& name);

    // 获取缓存的封面路径
    std::string getCachedPath(const std::string& gameName);

    std::string cacheDir_;
};

} // namespace rgh
