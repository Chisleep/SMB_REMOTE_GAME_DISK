// MountTool.cpp - M1 阶段命令行测试工具
// 用于验证 WinFsp 虚拟磁盘挂载功能
// 用法: rgh_mount <SMB主机> <共享名> <子路径> <盘符> [用户名] [密码]
// 示例: rgh_mount 192.168.5.103 Games ActionGame G user pass

#include "Common/Types.h"
#include "Common/Logger.h"
#include "Common/Config.h"
#include "SMB/SMBClient.h"
#include "SMB/ConnectionPool.h"
#include "VirtualDisk/VirtualDiskEngine.h"
#include "VirtualDisk/MemoryCache.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>

static std::atomic<bool> g_running{true};

static void signalHandler(int) {
    g_running = false;
}

static void printUsage() {
    printf("RemoteGameHub 虚拟盘挂载工具 (M1 测试)\n");
    printf("用法:\n");
    printf("  rgh_mount <SMB主机> <共享名> <子路径> <盘符> [用户名] [密码] [域]\n");
    printf("\n示例:\n");
    printf("  rgh_mount 192.168.5.103 Games ActionGame G user pass\n");
    printf("  rgh_mount 192.168.5.103 Games \"\" H\n");
    printf("\n挂载后，在资源管理器中打开 G: 盘即可访问远程游戏文件。\n");
    printf("按 Ctrl+C 卸载并退出。\n");
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        printUsage();
        return 1;
    }

    std::string host = argv[1];
    std::string share = argv[2];
    std::string subPath = argv[3];
    char driveLetter = argv[4][0];
    std::string username = argc > 5 ? argv[5] : "";
    std::string password = argc > 6 ? argv[6] : "";
    std::string domain = argc > 7 ? argv[7] : "";

    // 初始化日志
    rgh::Logger::instance().init("logs", rgh::LogLevel::Debug);

    printf("=== RemoteGameHub 虚拟盘挂载工具 ===\n");
    printf("SMB 主机: %s\n", host.c_str());
    printf("共享名:   %s\n", share.c_str());
    printf("子路径:   %s\n", subPath.empty() ? "(根)" : subPath.c_str());
    printf("盘符:     %c:\n", driveLetter);
    printf("用户名:   %s\n", username.empty() ? "(匿名)" : username.c_str());
    printf("\n");

    // 配置 SMB
    rgh::SmbServerConfig smbConfig;
    smbConfig.host = host;
    smbConfig.shareName = share;
    smbConfig.username = username;
    smbConfig.password = password;
    smbConfig.domain = domain;

    printf("[1/4] 初始化 SMB 连接池...\n");
    rgh::ConnectionPool::instance().init(smbConfig, 4);
    if (rgh::ConnectionPool::instance().totalConnections() == 0) {
        fprintf(stderr, "错误: SMB 连接失败\n");
        return 1;
    }
    printf("      SMB 连接成功，可用连接: %d\n", rgh::ConnectionPool::instance().totalConnections());

    // 测试连接延迟
    {
        auto borrower = rgh::ConnectionPool::instance().borrow();
        if (borrower) {
            uint32_t latency = borrower->pingLatencyMs();
            printf("      连接延迟: %u ms\n", latency);
        }
    }

    // 初始化内存缓存
    printf("[2/4] 初始化内存缓存 (512MB)...\n");
    rgh::MemoryCache::instance().init(512);

    // 挂载虚拟盘
    printf("[3/4] 挂载 WinFsp 虚拟盘 %c:...\n", driveLetter);
    rgh::VirtualDiskEngine vdEngine;

    vdEngine.setStatusCallback([](rgh::MountStatus status, const std::string& msg) {
        printf("      [状态] %s\n", msg.c_str());
    });

    if (!vdEngine.mount(subPath, driveLetter)) {
        fprintf(stderr, "错误: 虚拟盘挂载失败\n");
        rgh::ConnectionPool::instance().shutdown();
        return 1;
    }

    printf("\n[4/4] 挂载成功！\n");
    printf("\n========================================\n");
    printf("  虚拟盘 %c: 已挂载到 \\\\%s\\%s\\%s\n",
        driveLetter, host.c_str(), share.c_str(),
        subPath.empty() ? "" : subPath.c_str());
    printf("  打开资源管理器访问 %c:\\ 即可查看远程文件\n", driveLetter);
    printf("  按 Ctrl+C 卸载并退出\n");
    printf("========================================\n\n");

    // 设置信号处理
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // 主循环：等待用户退出，定期打印统计
    int counter = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        counter++;

        // 每 30 秒打印一次统计
        if (counter % 6 == 0) {
            const auto& ioStats = vdEngine.stats();
            const auto& cacheStats = rgh::MemoryCache::instance().stats();

            printf("\n--- 统计信息 ---\n");
            printf("IO 读取: %llu 次, %llu 字节\n",
                static_cast<unsigned long long>(ioStats.totalReads.load()),
                static_cast<unsigned long long>(ioStats.bytesRead.load()));
            printf("IO 写入: %llu 次, %llu 字节\n",
                static_cast<unsigned long long>(ioStats.totalWrites.load()),
                static_cast<unsigned long long>(ioStats.bytesWritten.load()));
            printf("缓存命中: %llu / %llu (%.1f%%)\n",
                static_cast<unsigned long long>(cacheStats.cacheHits.load()),
                static_cast<unsigned long long>(cacheStats.totalReads.load()),
                rgh::MemoryCache::instance().hitRate());
            printf("缓存大小: %llu KB\n",
                static_cast<unsigned long long>(cacheStats.bytesCached.load() / 1024));
            printf("----------------\n");
        }
    }

    // 卸载
    printf("\n正在卸载虚拟盘...\n");
    vdEngine.unmount();
    rgh::ConnectionPool::instance().shutdown();
    printf("已卸载，程序退出。\n");

    return 0;
}
