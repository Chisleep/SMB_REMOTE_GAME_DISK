#pragma once

#include "Common/Types.h"
#include <string>
#include <vector>
#include <atomic>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace rgh {

// 进程监控工具
// 提供进程枚举、子进程查找、进程树终止等功能
class ProcessMonitor {
public:
    // 获取所有运行中的进程
    struct ProcessEntry {
        uint32_t pid;
        uint32_t parentPid;
        std::string name;
        std::string exePath;
    };

    static std::vector<ProcessEntry> enumerateProcesses();

    // 获取指定进程的所有子进程（递归）
    static std::vector<uint32_t> getChildProcesses(uint32_t parentPid);

    // 获取进程信息
    static bool getProcessInfo(uint32_t pid, ProcessEntry& info);

    // 检查进程是否仍在运行
    static bool isProcessRunning(uint32_t pid);

    // 优雅终止进程（先 WM_CLOSE，超时后 TerminateProcess）
    static bool terminateProcess(uint32_t pid, int timeoutMs = 3000);

    // 终止进程树（包括所有子进程）
    static bool terminateProcessTree(uint32_t rootPid, int timeoutMs = 3000);

    // 等待进程退出
    static bool waitForExit(uint32_t pid, int timeoutMs = -1);

    // 获取进程 CPU 和内存使用率
    struct ProcessUsage {
        double cpuPercent = 0.0;
        uint64_t memoryBytes = 0;
        uint64_t handleCount = 0;
    };
    static bool getProcessUsage(uint32_t pid, ProcessUsage& usage);
};

} // namespace rgh
