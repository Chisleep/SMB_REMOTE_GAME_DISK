#include "GameLibrary/CoverFetcher.h"
#include "Common/Logger.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <shlwapi.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")
#endif

#include <sstream>
#include <algorithm>
#include <cctype>

namespace rgh {

CoverFetcher& CoverFetcher::instance() {
    static CoverFetcher fetcher;
    return fetcher;
}

void CoverFetcher::init(const std::string& cacheDir) {
    cacheDir_ = cacheDir;
#ifdef _WIN32
    CreateDirectoryA(cacheDir_.c_str(), nullptr);
#endif
    RGH_LOG_INFO("CoverFetcher", "封面缓存目录: " + cacheDir_);
}

std::string CoverFetcher::sanitizeFileName(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            result += '_';
        } else {
            result += c;
        }
    }
    return result;
}

std::string CoverFetcher::getCachedPath(const std::string& gameName) {
    return cacheDir_ + "\\" + sanitizeFileName(gameName) + ".jpg";
}

#ifdef _WIN32

std::string CoverFetcher::httpGet(const std::string& url) {
    // 解析 URL
    URL_COMPONENTSA urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    char hostName[256] = {0};
    char urlPath[2048] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 255;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 2047;

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &urlComp)) {
        return "";
    }

    // 创建会话
    HINTERNET hSession = WinHttpOpen(L"RemoteGameHub/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    // 创建连接
    std::wstring whost(hostName, hostName + strlen(hostName));
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(),
        urlComp.nPort ? urlComp.nPort : INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    // 创建请求
    std::wstring wpath(urlPath, urlPath + strlen(urlPath));
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

    // 发送请求
    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    }

    std::string result;
    if (bResults) {
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (dwSize > 0) {
                std::vector<char> buffer(dwSize + 1, 0);
                DWORD dwRead = 0;
                WinHttpReadData(hRequest, buffer.data(), dwSize, &dwRead);
                result.append(buffer.data(), dwRead);
            }
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

bool CoverFetcher::httpDownloadFile(const std::string& url, const std::string& savePath) {
    std::string data = httpGet(url);
    if (data.empty()) return false;

    FILE* fp = fopen(savePath.c_str(), "wb");
    if (!fp) return false;
    fwrite(data.data(), 1, data.size(), fp);
    fclose(fp);
    return true;
}

#else
std::string CoverFetcher::httpGet(const std::string&) { return ""; }
bool CoverFetcher::httpDownloadFile(const std::string&, const std::string&) { return false; }
#endif

std::string CoverFetcher::searchSteamAppId(const std::string& gameName) {
    // 使用 Steam 搜索 API
    // https://steamcommunity.com/actions/SearchApps/<gameName>
    std::string url = "https://steamcommunity.com/actions/SearchApps/" + gameName;
    std::string response = httpGet(url);

    if (response.empty()) return "";

    // 简化解析：查找第一个 appid
    // 响应格式: <li data-appid="123456">...
    size_t pos = response.find("data-appid=\"");
    if (pos == std::string::npos) return "";
    pos += 12; // "data-appid=\"" 长度
    size_t end = response.find('"', pos);
    if (end == std::string::npos) return "";
    return response.substr(pos, end - pos);
}

std::string CoverFetcher::downloadSteamCover(const std::string& appId, const std::string& savePath) {
    // Steam 封面 URL
    std::string url = "https://cdn.cloudflare.steamstatic.com/steam/apps/" +
        appId + "/header.jpg";
    if (httpDownloadFile(url, savePath)) {
        return savePath;
    }
    return "";
}

std::string CoverFetcher::fetchCover(const std::string& gameName,
                                      std::function<void(int)> progress) {
    if (cacheDir_.empty()) {
        RGH_LOG_WARN("CoverFetcher", "缓存目录未初始化");
        return "";
    }

    // 检查本地缓存
    std::string cachedPath = getCachedPath(gameName);
#ifdef _WIN32
    if (PathFileExistsA(cachedPath.c_str())) {
        RGH_LOG_DEBUG("CoverFetcher", "封面缓存命中: " + gameName);
        return cachedPath;
    }
#endif

    if (progress) progress(10);

    // 搜索 Steam AppId
    std::string appId = searchSteamAppId(gameName);
    if (appId.empty()) {
        RGH_LOG_DEBUG("CoverFetcher", "未找到 Steam 封面: " + gameName);
        return "";
    }

    if (progress) progress(50);

    // 下载封面
    std::string result = downloadSteamCover(appId, cachedPath);
    if (!result.empty()) {
        RGH_LOG_INFO("CoverFetcher", "封面下载成功: " + gameName + " (appid=" + appId + ")");
        if (progress) progress(100);
    }
    return result;
}

void CoverFetcher::fetchCovers(const std::vector<std::string>& gameNames,
                                std::function<void(int, int, const std::string&)> progress,
                                std::function<void(const std::string&, const std::string&)> onComplete) {
    int total = static_cast<int>(gameNames.size());
    for (int i = 0; i < total; ++i) {
        if (progress) progress(i + 1, total, gameNames[i]);
        std::string path = fetchCover(gameNames[i]);
        if (onComplete) onComplete(gameNames[i], path);
    }
}

void CoverFetcher::clearCache() {
#ifdef _WIN32
    WIN32_FIND_DATAA findData;
    std::string search = cacheDir_ + "\\*";
    HANDLE hFind = FindFirstFileA(search.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (findData.cFileName[0] != '.') {
                std::string path = cacheDir_ + "\\" + findData.cFileName;
                DeleteFileA(path.c_str());
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
#endif
    RGH_LOG_INFO("CoverFetcher", "封面缓存已清空");
}

} // namespace rgh
