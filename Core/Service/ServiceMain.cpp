#include "Service/MainService.h"
#include "Common/Logger.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>

#ifdef _WIN32

#define SERVICE_NAME L"RemoteGameHub"
#define SERVICE_DISPLAY_NAME L"RemoteGameHub 远程游戏运行服务"
#define SERVICE_DESCRIPTION L"通过 WinFsp 虚拟磁盘将 SMB 服务器上的游戏挂载到本地运行"

static SERVICE_STATUS g_serviceStatus = {};
static SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
static HANDLE g_stopEvent = nullptr;

static void reportServiceStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHint) {
    static DWORD checkPoint = 1;

    g_serviceStatus.dwCurrentState = currentState;
    g_serviceStatus.dwWin32ExitCode = win32ExitCode;
    g_serviceStatus.dwWaitHint = waitHint;

    if (currentState == SERVICE_START_PENDING) {
        g_serviceStatus.dwControlsAccepted = 0;
    } else {
        g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE;
    }

    if (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED) {
        g_serviceStatus.dwCheckPoint = 0;
    } else {
        g_serviceStatus.dwCheckPoint = checkPoint++;
    }

    SetServiceStatus(g_statusHandle, &g_serviceStatus);
}

static DWORD WINAPI serviceControlHandler(DWORD ctrlCode, DWORD eventType,
                                           LPVOID eventData, LPVOID context) {
    switch (ctrlCode) {
        case SERVICE_CONTROL_STOP:
            RGH_LOG_INFO("Service", "收到停止命令");
            reportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            if (g_stopEvent) {
                SetEvent(g_stopEvent);
            }
            return NO_ERROR;

        case SERVICE_CONTROL_PAUSE:
            reportServiceStatus(SERVICE_PAUSE_PENDING, NO_ERROR, 1000);
            // 暂停逻辑（简化：不实现）
            reportServiceStatus(SERVICE_PAUSED, NO_ERROR, 0);
            return NO_ERROR;

        case SERVICE_CONTROL_CONTINUE:
            reportServiceStatus(SERVICE_CONTINUE_PENDING, NO_ERROR, 1000);
            reportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;

        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static VOID WINAPI serviceMain(DWORD argc, LPTSTR* argv) {
    g_statusHandle = RegisterServiceCtrlHandlerExW(SERVICE_NAME, serviceControlHandler, nullptr);
    if (!g_statusHandle) {
        return;
    }

    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    reportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // 创建停止事件
    g_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        reportServiceStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    // 初始化服务
    if (!rgh::MainService::instance().init()) {
        RGH_LOG_ERROR("Service", "服务初始化失败");
        reportServiceStatus(SERVICE_STOPPED, ERROR_INIT_FAILURE, 0);
        return;
    }

    reportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
    RGH_LOG_INFO("Service", "服务已启动");

    // 在单独线程中运行服务主循环
    std::thread serviceThread([]() {
        rgh::MainService::instance().run();
    });

    // 等待停止信号
    WaitForSingleObject(g_stopEvent, INFINITE);

    // 停止服务
    rgh::MainService::instance().stop();
    if (serviceThread.joinable()) {
        serviceThread.join();
    }

    CloseHandle(g_stopEvent);
    reportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
    RGH_LOG_INFO("Service", "服务已停止");
}

// 安装服务
static bool installService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        wprintf(L"无法打开 SCM: %lu\n", GetLastError());
        return false;
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wcscat_s(exePath, L" -service");

    SC_HANDLE service = CreateServiceW(
        scm, SERVICE_NAME, SERVICE_DISPLAY_NAME,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        exePath, nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!service) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            wprintf(L"服务已存在\n");
        } else {
            wprintf(L"创建服务失败: %lu\n", err);
        }
        CloseServiceHandle(scm);
        return false;
    }

    // 设置描述
    SERVICE_DESCRIPTIONW desc = { const_cast<LPWSTR>(SERVICE_DESCRIPTION) };
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    wprintf(L"服务安装成功\n");
    return true;
}

// 卸载服务
static bool uninstallService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return false;

    SC_HANDLE service = OpenServiceW(scm, SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!service) {
        wprintf(L"服务不存在\n");
        CloseServiceHandle(scm);
        return false;
    }

    // 先停止服务
    SERVICE_STATUS status;
    ControlService(service, SERVICE_CONTROL_STOP, &status);

    DeleteService(service);
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    wprintf(L"服务卸载成功\n");
    return true;
}

#endif

// ===== 入口点 =====

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // 检查命令行参数
    bool runAsService = false;
    bool install = false;
    bool uninstall = false;
    bool consoleMode = false;

    for (int i = 1; i < argc; ++i) {
        if (_stricmp(argv[i], "-service") == 0) runAsService = true;
        else if (_stricmp(argv[i], "-install") == 0) install = true;
        else if (_stricmp(argv[i], "-uninstall") == 0) uninstall = true;
        else if (_stricmp(argv[i], "-console") == 0) consoleMode = true;
    }

    if (install) {
        return installService() ? 0 : 1;
    }

    if (uninstall) {
        return uninstallService() ? 0 : 1;
    }

    if (runAsService) {
        // 作为 Windows 服务运行
        SERVICE_TABLE_ENTRYW serviceTable[] = {
            { const_cast<LPWSTR>(SERVICE_NAME), serviceMain },
            { nullptr, nullptr }
        };

        if (!StartServiceCtrlDispatcherW(serviceTable)) {
            fprintf(stderr, "StartServiceCtrlDispatcher 失败: %lu\n", GetLastError());
            return 1;
        }
        return 0;
    }

    if (consoleMode) {
        // 控制台模式（调试用）
        printf("RemoteGameHub 服务 - 控制台模式\n");
        printf("按 Ctrl+C 退出\n\n");

        if (!rgh::MainService::instance().init()) {
            fprintf(stderr, "初始化失败\n");
            return 1;
        }

        // 设置 Ctrl+C 处理
        SetConsoleCtrlHandler([](DWORD ctrlType) -> BOOL {
            if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
                rgh::MainService::instance().stop();
                return TRUE;
            }
            return FALSE;
        }, TRUE);

        rgh::MainService::instance().run();
        return 0;
    }
#endif

    // 默认：显示帮助
    printf("RemoteGameHub Service\n");
    printf("用法:\n");
    printf("  RemoteGameHub.Service -install    安装为 Windows 服务\n");
    printf("  RemoteGameHub.Service -uninstall  卸载 Windows 服务\n");
    printf("  RemoteGameHub.Service -service    作为服务运行（由 SCM 调用）\n");
    printf("  RemoteGameHub.Service -console    控制台模式运行（调试用）\n");
    return 0;
}
