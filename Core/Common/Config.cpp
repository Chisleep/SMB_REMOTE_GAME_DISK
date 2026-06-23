#include "Common/Config.h"
#include "Common/Logger.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <fstream>
#include <sstream>

// 简易 JSON 解析（避免引入完整 JSON 库用于配置）
// 实际项目中可替换为 nlohmann/json
namespace {

std::string readFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return "";
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

// 极简 JSON 值提取（仅用于配置，非通用解析器）
std::string extractString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";
    pos++;
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

int64_t extractInt(const std::string& json, const std::string& key, int64_t defVal) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return defVal;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return defVal;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    try {
        return std::stoll(json.substr(pos));
    } catch (...) {
        return defVal;
    }
}

} // anonymous namespace

namespace rgh {

Config& Config::instance() {
    static Config cfg;
    return cfg;
}

Config::Config() {
    dataDir_ = defaultDataDir();
    logDir_ = defaultLogDir();
}

std::string Config::defaultConfigDir() {
#ifdef _WIN32
    char path[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::string(path) + "\\RemoteGameHub";
    }
    return "C:\\ProgramData\\RemoteGameHub";
#else
    return std::string(getenv("HOME")) + "/.remotegamehub";
#endif
}

std::string Config::defaultDataDir() {
    return defaultConfigDir() + "\\data";
}

std::string Config::defaultLogDir() {
    return defaultConfigDir() + "\\logs";
}

bool Config::load(const std::string& configPath) {
    configPath_ = configPath.empty() ? (defaultConfigDir() + "\\config.json") : configPath;

    std::string content = readFile(configPath_);
    if (content.empty()) {
        RGH_LOG_INFO("Config", "配置文件不存在，使用默认配置: " + configPath_);
        return false;
    }

    smbConfig_.host = extractString(content, "host");
    smbConfig_.shareName = extractString(content, "shareName");
    smbConfig_.username = extractString(content, "username");
    smbConfig_.password = extractString(content, "password");
    smbConfig_.domain = extractString(content, "domain");
    smbConfig_.port = static_cast<uint16_t>(extractInt(content, "port", 445));

    diskConfig_.driveLetter = extractString(content, "driveLetter").empty()
        ? 'G' : extractString(content, "driveLetter")[0];
    diskConfig_.memoryCacheSizeMB = extractInt(content, "memoryCacheSizeMB", 512);
    diskConfig_.enableReadAhead = extractInt(content, "enableReadAhead", 1) != 0;
    diskConfig_.readAheadSizeKB = extractInt(content, "readAheadSizeKB", 1024);
    diskConfig_.dirCacheTtlSec = static_cast<uint32_t>(extractInt(content, "dirCacheTtlSec", 30));

    RGH_LOG_INFO("Config", "配置加载成功，SMB主机: " + smbConfig_.host);
    return true;
}

bool Config::save() {
    if (configPath_.empty()) {
        configPath_ = defaultConfigDir() + "\\config.json";
    }

#ifdef _WIN32
    _mkdir(defaultConfigDir().c_str());
#else
    mkdir(defaultConfigDir().c_str(), 0755);
#endif

    std::ostringstream oss;
    oss << "{\n"
        << "  \"host\": \"" << smbConfig_.host << "\",\n"
        << "  \"port\": " << smbConfig_.port << ",\n"
        << "  \"shareName\": \"" << smbConfig_.shareName << "\",\n"
        << "  \"username\": \"" << smbConfig_.username << "\",\n"
        << "  \"password\": \"" << smbConfig_.password << "\",\n"
        << "  \"domain\": \"" << smbConfig_.domain << "\",\n"
        << "  \"driveLetter\": \"" << diskConfig_.driveLetter << "\",\n"
        << "  \"memoryCacheSizeMB\": " << diskConfig_.memoryCacheSizeMB << ",\n"
        << "  \"enableReadAhead\": " << (diskConfig_.enableReadAhead ? 1 : 0) << ",\n"
        << "  \"readAheadSizeKB\": " << diskConfig_.readAheadSizeKB << ",\n"
        << "  \"dirCacheTtlSec\": " << diskConfig_.dirCacheTtlSec << "\n"
        << "}\n";

    std::ofstream ofs(configPath_);
    if (!ofs.is_open()) {
        RGH_LOG_ERROR("Config", "无法写入配置文件: " + configPath_);
        return false;
    }
    ofs << oss.str();
    RGH_LOG_INFO("Config", "配置已保存: " + configPath_);
    return true;
}

} // namespace rgh
