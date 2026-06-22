#include "Launcher/ProcessMonitor.h"
#include "Common/Logger.h"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

#include <algorithm>

namespace rgh {

#ifdef _WIN32

std::vector<ProcessMonitor::ProcessEntry> ProcessMonitor::enumerateProcesses() {
    std::vector<ProcessEntry> processes;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return processes;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe)) {
        do {
            ProcessEntry entry;
            entry.pid = pe.th32ProcessID;
            entry.parentPid = pe.th32ParentProcessID;

            // 转换宽字符到 UTF-8
            int len = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1,
                nullptr, 0, nullptr, nullptr);
            entry.name.resize(len);
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1,
                entry.name.data(), len, nullptr, nullptr);
            if (!entry.name.empty() && entry.name.back() == '\0') {
                entry.name.pop_back();
            }

            // 获取完整路径
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                FALSE, pe.th32ProcessID);
            if (hProcess) {
                wchar_t path[MAX_PATH] = {0};
                if (GetModuleFileNameExW(hProcess, nullptr, path, MAX_PATH) > 0) {
                    int plen = WideCharToMultiByte(CP_UTF8, 0, path, -1,
                        nullptr, 0, nullptr, nullptr);
                    entry.exePath.resize(plen);
                    WideCharToMultiByte(CP_UTF8, 0, path, -1,
                        entry.exePath.data(), plen, nullptr, nullptr);
                    if (!entry.exePath.empty() && entry.exePath.back() == '\0') {
                        entry.exePath.pop_back();
                    }
                }
                CloseHandle(hProcess);
            }

            processes.push_back(entry);
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return processes;
}

std::vector<uint32_t> ProcessMonitor::getChildProcesses(uint32_t parentPid) {
    std::vector<uint32_t> children;
    auto processes = enumerateProcesses();

    // 递归查找子进程
    std::function<void(uint32_t)> findChildren = [&](uint32_t pid) {
        for (const auto& p : processes) {
            if (p.parentPid == pid) {
                children.push_back(p.pid);
                findChildren(p.pid);
            }
        }
    };

    findChildren(parentPid);
    return children;
}

bool ProcessMonitor::getProcessInfo(uint32_t pid, ProcessEntry& info) {
    auto processes = enumerateProcesses();
    for (const auto& p : processes) {
        if (p.pid == pid) {
            info = p;
            return true;
        }
    }
    return false;
}

bool ProcessMonitor::isProcessRunning(uint32_t pid) {
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!hProcess) return false;
    DWORD result = WaitForSingleObject(hProcess, 0);
    CloseHandle(hProcess);
    return result == WAIT_TIMEOUT;
}

bool ProcessMonitor::terminateProcess(uint32_t pid, int timeoutMs) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!hProcess) {
        RGH_LOG_WARN("ProcessMonitor", "OpenProcess 失败: " + std::to_string(GetLastError()));
        return false;
    }

    // 尝试优雅关闭
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == static_cast<DWORD>(lParam)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(pid));

    // 等待退出
    DWORD waitResult = WaitForSingleObject(hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        // 强制终止
        BOOL ok = TerminateProcess(hProcess, 1);
        if (ok) {
            WaitForSingleObject(hProcess, 2000);
        }
        CloseHandle(hProcess);
        return ok != 0;
    }

    CloseHandle(hProcess);
    return true;
}

bool ProcessMonitor::terminateProcessTree(uint32_t rootPid, int timeoutMs) {
    // 先获取所有子进程
    auto children = getChildProcesses(rootPid);

    // 从叶子到根终止
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        terminateProcess(*it, timeoutMs / 2);
    }
    return terminateProcess(rootPid, timeoutMs);
}

bool ProcessMonitor::waitForExit(uint32_t pid, int timeoutMs) {
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!hProcess) return true; // 进程不存在，视为已退出

    DWORD waitMs = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
    DWORD result = WaitForSingleObject(hProcess, waitMs);
    CloseHandle(hProcess);
    return result == WAIT_OBJECT_0;
}

bool ProcessMonitor::getProcessUsage(uint32_t pid, ProcessUsage& usage) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) return false;

    // 内存信息
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
        usage.memoryBytes = pmc.WorkingSetSize;
        usage.handleCount = 0;
    }

    // 句柄数
    DWORD handleCount = 0;
    GetProcessHandleCount(hProcess, &handleCount);
    usage.handleCount = handleCount;

    // CPU 使用率（需要两次采样，这里简化）
    FILETIME createTime, exitTime, kernelTime, userTime;
    if (GetProcessTimes(hProcess, &createTime, &exitTime, &kernelTime, &userTime)) {
        ULARGE_INTEGER kernel, user;
        kernel.LowPart = kernelTime.dwLowDateTime;
        kernel.HighPart = kernelTime.dwHighDateTime;
        user.LowPart = userTime.dwLowDateTime;
        user.HighPart = userTime.dwHighDateTime;
        // CPU 时间（100ns 单位）
        usage.cpuPercent = 0.0; // 需要两次采样计算
    }

    CloseHandle(hProcess);
    return true;
}

#else // 非 Windows 桩实现

std::vector<ProcessMonitor::ProcessEntry> ProcessMonitor::enumerateProcesses() { return {}; }
std::vector<uint32_t> ProcessMonitor::getChildProcesses(uint32_t) { return {}; }
bool ProcessMonitor::getProcessInfo(uint32_t, ProcessEntry&) { return false; }
bool ProcessMonitor::isProcessRunning(uint32_t) { return false; }
bool ProcessMonitor::terminateProcess(uint32_t, int) { return false; }
bool ProcessMonitor::terminateProcessTree(uint32_t, int) { return false; }
bool ProcessMonitor::waitForExit(uint32_t, int) { return false; }
bool ProcessMonitor::getProcessUsage(uint32_t, ProcessUsage&) { return false; }

#endif

} // namespace rgh
