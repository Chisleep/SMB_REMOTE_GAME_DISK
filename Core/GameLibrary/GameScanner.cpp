#include "GameLibrary/GameScanner.h"
#include "Common/Logger.h"

#include <algorithm>
#include <set>
#include <cctype>

namespace rgh {

// 非游戏 exe 黑名单（启动器、卸载器、配置工具等）
static const std::set<std::string> nonGameExes = {
    "uninstall", "uninst", "unsetup", "setup", "install",
    "launcher", "updater", "update", "config", "settings",
    "crashreporter", "errorreport", "redist", "dxsetup",
    "vcredist", "dotnet", "physx", "directx",
    "steam_api", "eossdk", "goggsdk",
    "readme", "manual", "help",
    "benchmark", "test", "diagnostic",
    "crashdump", "minidump",
};

GameScanner::GameScanner() {}

bool GameScanner::isNonGameExe(const std::string& exeName) {
    // 转小写比较
    std::string lower = exeName;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return std::tolower(c); });

    // 去掉 .exe 后缀
    if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".exe") {
        lower = lower.substr(0, lower.size() - 4);
    }

    // 检查黑名单
    for (const auto& bad : nonGameExes) {
        if (lower.find(bad) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string GameScanner::pickMainExe(const std::string& dirName,
                                       const std::vector<std::string>& exes) {
    if (exes.empty()) return "";

    // 优先选择与目录名相同的 exe
    std::string dirLower = dirName;
    std::transform(dirLower.begin(), dirLower.end(), dirLower.begin(),
        [](unsigned char c) { return std::tolower(c); });

    for (const auto& exe : exes) {
        std::string exeLower = exe;
        std::transform(exeLower.begin(), exeLower.end(), exeLower.begin(),
            [](unsigned char c) { return std::tolower(c); });
        if (exeLower.find(dirLower) != std::string::npos) {
            return exe;
        }
    }

    // 过滤掉非游戏 exe
    std::vector<std::string> gameExes;
    for (const auto& exe : exes) {
        if (!isNonGameExe(exe)) {
            gameExes.push_back(exe);
        }
    }

    if (!gameExes.empty()) {
        // 选择最大的 exe（通常是游戏主程序）
        return gameExes.front();
    }

    // 回退：返回第一个
    return exes.front();
}

std::vector<std::string> GameScanner::findExesInDir(const std::string& dirPath) {
    std::vector<std::string> exes;
    if (!smbClient_) return exes;

    std::vector<RemoteFileInfo> entries;
    if (!smbClient_->listDirectory(dirPath, entries)) return exes;

    for (const auto& entry : entries) {
        if (entry.isDirectory) continue;
        // 检查 .exe 扩展名
        std::string name = entry.name;
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return std::tolower(c); });
        if (name.size() > 4 && name.substr(name.size() - 4) == ".exe") {
            exes.push_back(entry.name);
        }
    }
    return exes;
}

void GameScanner::calculateDirSize(const std::string& path, uint64_t& totalSize,
                                    int& fileCount, int depth, int maxDepth) {
    if (!smbClient_ || depth > maxDepth) return;

    std::vector<RemoteFileInfo> entries;
    if (!smbClient_->listDirectory(path, entries)) return;

    for (const auto& entry : entries) {
        if (entry.isDirectory) {
            std::string subPath = path.empty() ? entry.name : (path + "\\" + entry.name);
            calculateDirSize(subPath, totalSize, fileCount, depth + 1, maxDepth);
        } else {
            totalSize += entry.size;
            ++fileCount;
        }
    }
}

std::optional<ScanResult> GameScanner::scanSingleDirectory(const std::string& dirPath) {
    if (!smbClient_) return std::nullopt;

    // 获取目录名
    std::string dirName = dirPath;
    size_t pos = dirName.find_last_of('\\');
    if (pos != std::string::npos) {
        dirName = dirName.substr(pos + 1);
    }

    // 查找 exe
    auto exes = findExesInDir(dirPath);
    if (exes.empty()) return std::nullopt;

    ScanResult result;
    result.name = dirName;
    result.smbPath = dirPath;
    result.allExes = exes;
    result.exeRelativePath = pickMainExe(dirName, exes);

    // 检查是否有启动器
    for (const auto& exe : exes) {
        std::string lower = exe;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return std::tolower(c); });
        if (lower.find("launcher") != std::string::npos) {
            result.hasLauncher = true;
            break;
        }
    }

    // 计算目录大小（浅层，避免太慢）
    calculateDirSize(dirPath, result.totalSize, result.fileCount, 0, 2);

    return result;
}

std::vector<ScanResult> GameScanner::scanDirectory(
    const std::string& rootPath,
    std::function<void(int, int, const std::string&)> progress) {

    std::vector<ScanResult> results;
    if (!smbClient_) {
        RGH_LOG_ERROR("GameScanner", "SMB 客户端未设置");
        return results;
    }

    RGH_LOG_INFO("GameScanner", "开始扫描: " + rootPath);

    // 枚举根目录下的子目录
    std::vector<RemoteFileInfo> entries;
    if (!smbClient_->listDirectory(rootPath, entries)) {
        RGH_LOG_ERROR("GameScanner", "无法枚举目录: " + rootPath);
        return results;
    }

    // 收集所有子目录
    std::vector<std::string> subDirs;
    for (const auto& entry : entries) {
        if (entry.isDirectory) {
            std::string path = rootPath.empty() ? entry.name : (rootPath + "\\" + entry.name);
            subDirs.push_back(path);
        }
    }

    // 也检查根目录本身是否包含 exe（单游戏目录）
    auto rootExes = findExesInDir(rootPath);
    if (!rootExes.empty()) {
        auto result = scanSingleDirectory(rootPath);
        if (result) {
            results.push_back(*result);
        }
    }

    // 扫描每个子目录
    int total = static_cast<int>(subDirs.size());
    for (int i = 0; i < total; ++i) {
        if (progress) {
            progress(i + 1, total, subDirs[i]);
        }

        auto result = scanSingleDirectory(subDirs[i]);
        if (result) {
            RGH_LOG_INFO("GameScanner", "发现游戏: " + result->name +
                " (exe: " + result->exeRelativePath + ")");
            results.push_back(*result);
        }
    }

    RGH_LOG_INFO("GameScanner", "扫描完成，共发现 " + std::to_string(results.size()) + " 个游戏");
    return results;
}

} // namespace rgh
