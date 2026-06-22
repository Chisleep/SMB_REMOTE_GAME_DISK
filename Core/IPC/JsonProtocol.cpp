#include "IPC/JsonProtocol.h"
#include "Common/Logger.h"

#include <sstream>
#include <random>
#include <chrono>
#include <algorithm>

namespace rgh {

// ===== Action 枚举转换 =====

static const struct { IpcAction action; const char* name; } actionMap[] = {
    {IpcAction::Ping,               "ping"},
    {IpcAction::Pong,               "pong"},
    {IpcAction::GetStatus,          "get_status"},
    {IpcAction::StatusResponse,     "status_response"},
    {IpcAction::TestSmbConnection,  "test_smb"},
    {IpcAction::SmbConnectionResult,"smb_result"},
    {IpcAction::SaveConfig,         "save_config"},
    {IpcAction::LoadConfig,         "load_config"},
    {IpcAction::ConfigResponse,     "config_response"},
    {IpcAction::ScanGames,          "scan_games"},
    {IpcAction::ScanProgress,       "scan_progress"},
    {IpcAction::ScanComplete,       "scan_complete"},
    {IpcAction::AddGame,            "add_game"},
    {IpcAction::RemoveGame,         "remove_game"},
    {IpcAction::UpdateGame,         "update_game"},
    {IpcAction::GetGameList,        "get_game_list"},
    {IpcAction::GameListResponse,   "game_list_response"},
    {IpcAction::GetGameInfo,        "get_game_info"},
    {IpcAction::GameInfoResponse,   "game_info_response"},
    {IpcAction::LaunchGame,         "launch_game"},
    {IpcAction::LaunchProgress,     "launch_progress"},
    {IpcAction::LaunchComplete,     "launch_complete"},
    {IpcAction::LaunchFailed,       "launch_failed"},
    {IpcAction::TerminateGame,      "terminate_game"},
    {IpcAction::GameExited,         "game_exited"},
    {IpcAction::GetStats,           "get_stats"},
    {IpcAction::StatsResponse,      "stats_response"},
    {IpcAction::GetPlayRecords,     "get_play_records"},
    {IpcAction::PlayRecordsResponse,"play_records_response"},
    {IpcAction::GetDiskStats,       "get_disk_stats"},
    {IpcAction::DiskStatsResponse,  "disk_stats_response"},
    {IpcAction::Error,              "error"},
};

const char* JsonProtocol::actionToString(IpcAction action) {
    for (const auto& e : actionMap) {
        if (e.action == action) return e.name;
    }
    return "unknown";
}

IpcAction JsonProtocol::stringToAction(const std::string& str) {
    for (const auto& e : actionMap) {
        if (e.name == str) return e.action;
    }
    return IpcAction::Error;
}

// ===== JSON 转义 =====

std::string JsonProtocol::escapeJson(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

std::string JsonProtocol::unescapeJson(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '"':  result += '"';  ++i; break;
                case '\\': result += '\\'; ++i; break;
                case '/':  result += '/';  ++i; break;
                case 'n':  result += '\n'; ++i; break;
                case 'r':  result += '\r'; ++i; break;
                case 't':  result += '\t'; ++i; break;
                case 'u':
                    if (i + 5 < s.size()) {
                        // 简化处理：跳过 unicode 转义
                        i += 5;
                    }
                    break;
                default: result += s[i]; break;
            }
        } else {
            result += s[i];
        }
    }
    return result;
}

// ===== SessionId 生成 =====

std::string JsonProtocol::generateSessionId() {
    static std::mt19937_64 rng(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream oss;
    oss << std::hex << rng() << rng();
    return oss.str().substr(0, 16);
}

// ===== 简易 JSON 值提取 =====

namespace {

std::string extractValue(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) pos++;
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        size_t end = pos + 1;
        while (end < json.size() && json[end] != '"') {
            if (json[end] == '\\') ++end;
            ++end;
        }
        return JsonProtocol::unescapeJson(json.substr(pos + 1, end - pos - 1));
    } else {
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']') ++end;
        return json.substr(pos, end - pos);
    }
}

std::map<std::string, std::string> extractParams(const std::string& json, const std::string& key) {
    std::map<std::string, std::string> result;
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return result;
    pos = json.find('{', pos);
    if (pos == std::string::npos) return result;

    int depth = 1;
    size_t start = pos + 1;
    size_t i = pos + 1;
    while (i < json.size() && depth > 0) {
        if (json[i] == '{') depth++;
        else if (json[i] == '}') depth--;
        if (depth == 0) break;
        ++i;
    }
    std::string paramsJson = json.substr(start, i - start);

    // 提取键值对
    size_t j = 0;
    while (j < paramsJson.size()) {
        size_t kStart = paramsJson.find('"', j);
        if (kStart == std::string::npos) break;
        size_t kEnd = paramsJson.find('"', kStart + 1);
        if (kEnd == std::string::npos) break;
        std::string k = paramsJson.substr(kStart + 1, kEnd - kStart - 1);

        size_t colon = paramsJson.find(':', kEnd);
        if (colon == std::string::npos) break;
        size_t vStart = colon + 1;
        while (vStart < paramsJson.size() && paramsJson[vStart] == ' ') vStart++;

        if (vStart < paramsJson.size() && paramsJson[vStart] == '"') {
            size_t vEnd = vStart + 1;
            while (vEnd < paramsJson.size() && paramsJson[vEnd] != '"') {
                if (paramsJson[vEnd] == '\\') ++vEnd;
                ++vEnd;
            }
            result[k] = JsonProtocol::unescapeJson(paramsJson.substr(vStart + 1, vEnd - vStart - 1));
            j = vEnd + 1;
        } else {
            size_t vEnd = vStart;
            while (vEnd < paramsJson.size() && paramsJson[vEnd] != ',' && paramsJson[vEnd] != '}') vEnd++;
            result[k] = paramsJson.substr(vStart, vEnd - vStart);
            j = vEnd;
        }
    }
    return result;
}

} // anonymous namespace

// ===== 序列化 =====

std::string JsonProtocol::serializeRequest(const IpcRequest& req) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"action\":\"" << escapeJson(actionToString(req.action)) << "\"";
    oss << ",\"sessionId\":\"" << escapeJson(req.sessionId) << "\"";

    if (!req.params.empty()) {
        oss << ",\"params\":{";
        bool first = true;
        for (const auto& [k, v] : req.params) {
            if (!first) oss << ",";
            oss << "\"" << escapeJson(k) << "\":\"" << escapeJson(v) << "\"";
            first = false;
        }
        oss << "}";
    }

    if (!req.jsonData.empty()) {
        oss << ",\"data\":" << req.jsonData;
    }
    oss << "}";
    return oss.str();
}

std::string JsonProtocol::serializeResponse(const IpcResponse& resp) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"action\":\"" << escapeJson(actionToString(resp.action)) << "\"";
    oss << ",\"sessionId\":\"" << escapeJson(resp.sessionId) << "\"";
    oss << ",\"success\":" << (resp.success ? "true" : "false");

    if (!resp.error.empty()) {
        oss << ",\"error\":\"" << escapeJson(resp.error) << "\"";
    }

    if (!resp.params.empty()) {
        oss << ",\"params\":{";
        bool first = true;
        for (const auto& [k, v] : resp.params) {
            if (!first) oss << ",";
            oss << "\"" << escapeJson(k) << "\":\"" << escapeJson(v) << "\"";
            first = false;
        }
        oss << "}";
    }

    if (!resp.jsonData.empty()) {
        oss << ",\"data\":" << resp.jsonData;
    }
    oss << "}";
    return oss.str();
}

// ===== 反序列化 =====

IpcRequest JsonProtocol::parseRequest(const std::string& json) {
    IpcRequest req;
    req.action = stringToAction(extractValue(json, "action"));
    req.sessionId = extractValue(json, "sessionId");
    req.params = extractParams(json, "params");

    // 提取 data 字段（原始 JSON）
    size_t dataPos = json.find("\"data\"");
    if (dataPos != std::string::npos) {
        size_t colon = json.find(':', dataPos);
        if (colon != std::string::npos) {
            size_t start = colon + 1;
            while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) start++;
            if (start < json.size() && (json[start] == '{' || json[start] == '[')) {
                char open = json[start];
                char close = (open == '{') ? '}' : ']';
                int depth = 1;
                size_t end = start + 1;
                while (end < json.size() && depth > 0) {
                    if (json[end] == open) depth++;
                    else if (json[end] == close) depth--;
                    if (depth == 0) break;
                    ++end;
                }
                req.jsonData = json.substr(start, end - start + 1);
            }
        }
    }
    return req;
}

IpcResponse JsonProtocol::parseResponse(const std::string& json) {
    IpcResponse resp;
    resp.action = stringToAction(extractValue(json, "action"));
    resp.sessionId = extractValue(json, "sessionId");
    std::string success = extractValue(json, "success");
    resp.success = (success == "true" || success == "1");
    resp.error = extractValue(json, "error");
    resp.params = extractParams(json, "params");

    size_t dataPos = json.find("\"data\"");
    if (dataPos != std::string::npos) {
        size_t colon = json.find(':', dataPos);
        if (colon != std::string::npos) {
            size_t start = colon + 1;
            while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) start++;
            if (start < json.size() && (json[start] == '{' || json[start] == '[')) {
                char open = json[start];
                char close = (open == '{') ? '}' : ']';
                int depth = 1;
                size_t end = start + 1;
                while (end < json.size() && depth > 0) {
                    if (json[end] == open) depth++;
                    else if (json[end] == close) depth--;
                    if (depth == 0) break;
                    ++end;
                }
                resp.jsonData = json.substr(start, end - start + 1);
            }
        }
    }
    return resp;
}

} // namespace rgh
