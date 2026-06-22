#include "Common/CredentialStore.h"
#include "Common/Logger.h"

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#pragma comment(lib, "advapi32.lib")
#endif

#include <sstream>

namespace rgh {

CredentialStore& CredentialStore::instance() {
    static CredentialStore store;
    return store;
}

std::string CredentialStore::makeTargetName(const std::string& host, uint16_t port) {
    std::ostringstream oss;
    oss << "RemoteGameHub:" << host << ":" << port;
    return oss.str();
}

bool CredentialStore::saveCredential(const std::string& host, uint16_t port,
                                      const std::string& username,
                                      const std::string& password,
                                      const std::string& domain) {
#ifdef _WIN32
    std::string target = makeTargetName(host, port);

    // 用户名格式: domain\username 或 username
    std::string fullUsername = domain.empty() ? username : (domain + "\\" + username);

    CREDENTIALA cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<char*>(target.c_str());
    cred.UserName = const_cast<char*>(fullUsername.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(password.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(password.data()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.AttributeCount = 0;

    if (!CredWriteA(&cred, 0)) {
        RGH_LOG_ERROR("CredentialStore", "CredWrite 失败，错误码: " + std::to_string(GetLastError()));
        return false;
    }

    RGH_LOG_INFO("CredentialStore", "凭据已保存: " + target);
    return true;
#else
    // 非 Windows 平台仅记录（开发环境无法使用）
    RGH_LOG_WARN("CredentialStore", "非 Windows 平台，凭据存储不可用");
    return false;
#endif
}

bool CredentialStore::loadCredential(const std::string& host, uint16_t port,
                                      std::string& username,
                                      std::string& password,
                                      std::string& domain) {
#ifdef _WIN32
    std::string target = makeTargetName(host, port);
    PCREDENTIALA pCred = nullptr;

    if (!CredReadA(target.c_str(), CRED_TYPE_GENERIC, 0, &pCred)) {
        RGH_LOG_WARN("CredentialStore", "CredRead 失败: " + std::to_string(GetLastError()));
        return false;
    }

    username = pCred->UserName ? pCred->UserName : "";

    // 解析 domain\username
    size_t pos = username.find('\\');
    if (pos != std::string::npos) {
        domain = username.substr(0, pos);
        username = username.substr(pos + 1);
    }

    if (pCred->CredentialBlob && pCred->CredentialBlobSize > 0) {
        password.assign(reinterpret_cast<const char*>(pCred->CredentialBlob),
                        pCred->CredentialBlobSize);
    }

    CredFree(pCred);
    RGH_LOG_DEBUG("CredentialStore", "凭据读取成功: " + target);
    return true;
#else
    return false;
#endif
}

bool CredentialStore::deleteCredential(const std::string& host, uint16_t port) {
#ifdef _WIN32
    std::string target = makeTargetName(host, port);
    if (!CredDeleteA(target.c_str(), CRED_TYPE_GENERIC, 0)) {
        RGH_LOG_WARN("CredentialStore", "CredDelete 失败: " + std::to_string(GetLastError()));
        return false;
    }
    RGH_LOG_INFO("CredentialStore", "凭据已删除: " + target);
    return true;
#else
    return false;
#endif
}

} // namespace rgh
