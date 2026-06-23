#pragma once

#include <string>

namespace rgh {

// SMB 凭据安全存储，使用 Windows Credential Manager
// 避免明文存储密码
class CredentialStore {
public:
    static CredentialStore& instance();

    // 保存 SMB 凭据到 Windows 凭据管理器
    bool saveCredential(const std::string& host, uint16_t port,
                        const std::string& username,
                        const std::string& password,
                        const std::string& domain = "");

    // 读取 SMB 凭据
    bool loadCredential(const std::string& host, uint16_t port,
                        std::string& username,
                        std::string& password,
                        std::string& domain);

    // 删除凭据
    bool deleteCredential(const std::string& host, uint16_t port);

    // 生成凭据目标名，如 "RemoteGameHub:192.168.5.103:445"
    static std::string makeTargetName(const std::string& host, uint16_t port);

private:
    CredentialStore() = default;
};

} // namespace rgh
