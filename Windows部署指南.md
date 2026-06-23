# RemoteGameHub — Windows 部署与测试指南

> 目标：把 `/workspace/` 的代码下载到 Windows 10/11，安装 WinFsp + SQLite，编译 C++ 核心与 WPF GUI，然后用 `rgh_mount.exe` 挂载 SMB 远程目录进行测试。

---

## 0. 代码下载方式（三选一）

### 方式 A：你能访问 git 仓库
```bat
git clone <你的仓库地址>  C:\RemoteGameHub
cd /d C:\RemoteGameHub
```

### 方式 B：使用 ZIP 下载（推荐）
在远程沙箱里执行一次 `tar` 打包：
```bash
cd /workspace && tar --exclude='.git' -czvf /tmp/RGH-src.zip .
```
把 `RGH-src.zip` 下载到 Windows，解压到 `C:\RemoteGameHub`

### 方式 C：scp / rsync / Samba
```bash
scp -r /workspace/ user@192.168.5.106:C:/RemoteGameHub/
```
或在 Windows 上把沙箱目录挂成 SMB 再复制。

---

## 1. 快速一键部署

进入 `C:\RemoteGameHub`，以 **管理员身份** 打开 PowerShell 或 CMD：

```bat
cd /d C:\RemoteGameHub

REM =============================================================
REM   方案 A：全自动（推荐第一次使用）
REM =============================================================
DevEnvSetup.bat

REM =============================================================
REM   方案 B：一键编译（已装过 VS/SDK/WinFsp）
REM =============================================================
BuildAll.bat

REM =============================================================
REM   方案 C：M1 手动快速验证（编译完后）
REM =============================================================
QuickTest.bat
```

### 自动做的事

| 步骤 | 内容 |
|------|------|
| ① | 下载并静默安装 WinFsp 2.0.2405 (驱动 + SDK → `C:\Program Files (x86)\WinFsp`) |
| ② | 下载 SQLite amalgamation → `ThirdParty/sqlite/` |
| ③ | 把 `sqlite3.c` 编译成 `ThirdParty/sqlite/build/sqlite3.lib`（静态库） |
| ④ | CMake + VS2022 编译 C++ 核心 → `build/Release/rgh_mount.exe`、`RemoteGameHub.Service.exe` |
| ⑤ | dotnet build WPF GUI → `GUI/RemoteGameHub.UI/bin/Release/net8.0-windows/` |

---

## 2. 前置条件（如 DevEnvSetup 失败）

如因网络原因 DevEnvSetup.bat 无法下载，可手动准备：

| 组件 | 安装/下载 |
|------|-----------|
| Visual Studio 2022 (Community 免费) | https://visualstudio.microsoft.com/zh-hans/ 勾选“C++桌面开发”+“.NET桌面开发” |
| .NET 8 SDK | `winget install Microsoft.DotNet.SDK.8` |
| CMake | `winget install Kitware.CMake` |
| WinFsp (核心依赖) | https://winfsp.dev/rel/ 下载 `winfsp-<版本>.msi`，安装时勾选 Driver |
| SQLite amalgamation | https://sqlite.org/download.html 下载 `sqlite-amalgamation-*.zip`，把 `sqlite3.c/h` 放到 `ThirdParty/sqlite/`，然后执行 `BuildSQLite.bat` |

---

## 3. 手动编译步骤（如果 BuildAll.bat 失败）

### 3.1 在 VS 开发者终端编译 SQLite
```bat
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d C:\RemoteGameHub
BuildSQLite.bat
```
产物 → `ThirdParty/sqlite/build/sqlite3.lib`

### 3.2 CMake 配置 + 构建 C++ 核心
```bat
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ^
      -DWINFSP_DIR="C:/Program Files (x86)/WinFsp" ^
      -DSQLITE3_DIR="C:/RemoteGameHub/ThirdParty/sqlite/build" ^
      ..
cmake --build . --config Release --parallel %NUMBER_OF_PROCESSORS%
```

产物：
- `build/Release/rgh_mount.exe`          ← M1 命令行测试工具
- `build/Release/RemoteGameHub.Service.exe`  ← 完整后台服务 (GUI 通信)

### 3.3 编译 WPF GUI
```bat
cd C:\RemoteGameHub\GUI\RemoteGameHub.UI
dotnet build -c Release
```

---

## 4. M1 测试：挂载 SMB 目录到本地盘符

### 4.1 验证 SMB 网络可达
```bat
ping 192.168.5.103     # 必须通
net view \\192.168.5.103 /all    # 应能看到 Games 共享
```

### 4.2 用 `rgh_mount.exe` 挂载

假设你在 SMB 服务器上有一个目录 `\\192.168.5.103\Games\SomeGame` （里面有 game.exe 和资源），用本地 G 盘挂载：

```bat
cd C:\RemoteGameHub\build\Release

REM  语法: rgh_mount.exe <SMB主机> <共享名> <子目录> <盘符> [用户名] [密码]
rgh_mount.exe 192.168.5.103 Games SomeGame G yourusername yourpassword

REM （子目录可为空） rgh_mount.exe 192.168.5.103 Games "" G yourusername yourpassword
```

### 4.3 预期的日志输出
```
=== RemoteGameHub 虚拟盘挂载工具 ===
SMB 主机: 192.168.5.103
共享名:   Games
子路径:   SomeGame
盘符:     G:
...
[1/4] 初始化 SMB 连接池...
      SMB 连接成功, 可用连接: 4
      连接延迟: 2 ms
[2/4] 初始化内存缓存 (512MB)...
[3/4] 挂载 WinFsp 虚拟盘 G:...
      [状态] 正在挂载虚拟盘 G:
      [状态] 虚拟盘已挂载: G:

[4/4] 挂载成功！

========================================================================
  虚拟盘 G: 已挂载到 \\192.168.5.103\Games\SomeGame
  打开资源管理器访问 G:\ 即可查看远程文件
  按 Ctrl+C 卸载并退出
========================================================================
```

### 4.4 测试步骤
1. 打开资源管理器 → `G:\`，确认能看到文件目录
2. 双击 `game.exe` 测试游戏能否启动（反作弊游戏可能失败，符合预期）
3. Ctrl+C 终止程序，确认盘符消失

### 4.5 常见故障排查

| 现象 | 可能原因 | 解决 |
|------|----------|------|
| `rgh_mount.exe` 启动即退出，报错 “SMB 连接失败” | SMB IP/用户名/密码错误 | 用 `net use \\192.168.5.103\ipc$ /user:用户名 密码` 先在 CMD 里测一下凭据 |
| 能挂载，但 `G:\` 打开是空的 | SMB 路径错误，或 WinFsp 回调失败 | 查看 `logs/RGH-*.log` |
| 游戏启动后卡死在 0% 或报错“磁盘未就绪” | 内存缓存未就绪 / SMB 延迟高 | 检查 `ping -t 192.168.5.103` 延迟 |
| 游戏提示“反作弊检测到虚拟环境” | 某些反作弊会拒绝 WinFsp 挂载的盘 | 属于技术限制，无法绕开 |
| 游戏启动后闪退，日志显示 STATUS_ACCESS_DENIED | 权限不足 | 尝试以管理员身份运行 `rgh_mount.exe` |
| BuildAll.bat 报 “vcvars64.bat 找不到” | 未装 VS2022 或路径错误 | 手动调用 `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat` 后再试 |

---

## 5. M2+ 完整服务测试（等 C++ 核心编译成功）

```bat
REM 以控制台模式启动后台服务（调试用，不注册服务）
cd /d C:\RemoteGameHub\build\Release
RemoteGameHub.Service.exe -console

REM 另开一个 CMD 启动 GUI
cd C:\RemoteGameHub\GUI\RemoteGameHub.UI\bin\Release\net8.0-windows
RemoteGameHub.UI.exe
```

GUI 里操作：
1. 设置 → 填 SMB 配置 → 测试连接
2. 游戏库 → 扫描 → 自动识别目录里的 `*.exe`
3. 启动 → 后台服务自动挂载 G: 并启动所选 exe

---

## 6. 日志目录

- C++ 服务：`C:\Users\<你>\AppData\Roaming\RemoteGameHub\logs\`
- WPF GUI：控制台窗口可看到与 Pipe 相关的错误日志

---

## 7. 文件一览（你现在拿到的这个仓库里）

```
/workspace/
├── DevEnvSetup.bat          ← 一键安装 WinFsp/SQLite 并编译
├── BuildAll.bat             ← 一键 VS2022 编译（仅编译）
├── BuildSQLite.bat          ← 把 SQLite amalgamation 编译成静态库
├── QuickTest.bat            ← 交互式测试脚本 (M1)
├── CMakeLists.txt           ← 新版构建系统 (VS2022)
├── Windows部署指南.md       ← 本文档
├── Core/                    ← C++ 核心代码 (~40 文件)
│   ├── Common/              ├─ 日志/配置/凭据
│   ├── SMB/                 ├─ 客户端 + 连接池
│   ├── VirtualDisk/         ├─ WinFsp 虚拟盘 + 内存缓存
│   ├── GameLibrary/         ├─ 扫描/识别/SQLite 游戏库
│   ├── Launcher/            ├─ 启动管理/进程监控
│   ├── Stats/               ├─ 游玩统计
│   ├── IPC/                 ├─ 命名管道通信
│   └── Service/             └─ MainService + Service 注册
└── GUI/RemoteGameHub.UI/    ← C# .NET 8 WPF GUI
    ├── Models/ / Services/ / ViewModels/
    └── Views/                窗口 + 页面
```

---

## 8. 下一步建议

1. **M1（必做）** — 完成 `rgh_mount.exe` 的测试，确认 WinFsp 能正确把远程目录映射成本地盘，`G:\` 可打开，双击 exe 能运行（反作弊游戏可能失败，属于预期）。
2. **M2** — 启动游戏库 GUI，完成 SMB 配置→扫描→手动添加→启动流程。
3. **M3** — 把后台服务注册成 Windows 服务 (`-install`)，开机自启并通过命名管道与 GUI 通信。

祝你使用顺利！
