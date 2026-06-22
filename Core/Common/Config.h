#pragma once

#include "Common/Types.h"
#include <string>

namespace rgh {

// 应用全局配置管理，持久化到 JSON 文件
class Config {
public:
    static Config& instance();

    // 加载配置文件，默认路径 %APPDATA%/RemoteGameHub/config.json
    bool load(const std::string& configPath = "");
    bool save();

    // 配置访问器
    const SmbServerConfig& smbConfig() const { return smbConfig_; }
    void setSmbConfig(const SmbServerConfig& cfg) { smbConfig_ = cfg; }

    const VirtualDiskConfig& diskConfig() const { return diskConfig_; }
    void setDiskConfig(const VirtualDiskConfig& cfg) { diskConfig_ = cfg; }

    // 数据目录（游戏数据库、封面缓存等）
    const std::string& dataDir() const { return dataDir_; }
    const std::string& logDir() const { return logDir_; }

    // 默认配置目录
    static std::string defaultConfigDir();
    static std::string defaultDataDir();
    static std::string defaultLogDir();

private:
    Config();

    SmbServerConfig smbConfig_;
    VirtualDiskConfig diskConfig_;
    std::string configPath_;
    std::string dataDir_;
    std::string logDir_;
};

} // namespace rgh
