#pragma once

#include "Common/Types.h"
#include "SMB/SMBClient.h"
#include <string>
#include <vector>
#include <functional>

namespace rgh {

// 扫描结果项
struct ScanResult {
    std::string name;              // 游戏名（目录名）
    std::string smbPath;           // SMB 相对路径
    std::string exeRelativePath;   // 主 exe 路径
    std::vector<std::string> allExes; // 所有 exe
    bool hasLauncher = false;      // 是否有启动器
    uint64_t totalSize = 0;        // 目录总大小
    int fileCount = 0;             // 文件数
};

// 游戏扫描器
// 扫描 SMB 共享目录，识别游戏（启发式判断）
class GameScanner {
public:
    GameScanner();

    // 设置 SMB 客户端
    void setSmbClient(std::shared_ptr<SMBClient> client) { smbClient_ = client; }

    // 扫描指定目录下的游戏
    // 每个子目录如果包含 .exe 则认为是游戏
    // progress 回调：(当前/总数, 当前目录名)
    std::vector<ScanResult> scanDirectory(
        const std::string& rootPath,
        std::function<void(int, int, const std::string&)> progress = nullptr);

    // 扫描单个目录，判断是否为游戏
    std::optional<ScanResult> scanSingleDirectory(const std::string& dirPath);

    // 启发式选择主 exe（优先选择名字与目录名相同的 exe）
    static std::string pickMainExe(const std::string& dirName, const std::vector<std::string>& exes);

    // 已知的非游戏 exe（启动器、卸载器等）
    static bool isNonGameExe(const std::string& exeName);

private:
    std::shared_ptr<SMBClient> smbClient_;

    // 递归计算目录大小和文件数（限制深度避免过深）
    void calculateDirSize(const std::string& path, uint64_t& totalSize,
                          int& fileCount, int depth = 0, int maxDepth = 3);

    // 收集目录中的所有 exe
    std::vector<std::string> findExesInDir(const std::string& dirPath);
};

} // namespace rgh
