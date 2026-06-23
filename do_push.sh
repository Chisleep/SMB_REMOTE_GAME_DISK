#!/bin/bash
set -e
cd /workspace
echo "--- step 1: config"
git config user.name Chisleep
git config user.email "chisleep@users.noreply.github.com"
echo "--- step 2: add"
git add -A
echo "--- step 3: status"
git status --short | wc -l
echo "--- step 4: commit"
git commit -m "init: RemoteGameHub - WinFsp虚拟盘 + SMB客户端 + 游戏库管理 + WPF GUI

项目结构:
- Core/Common: 日志/配置/凭据存储
- Core/SMB: SMB客户端 + 连接池
- Core/VirtualDisk: WinFsp虚拟盘引擎 + 内存缓存(LRU)
- Core/GameLibrary: 游戏扫描器 + 封面抓取 + SQLite数据库
- Core/Launcher: 启动管理器 + 进程监控
- Core/Stats: 游玩统计记录
- Core/IPC: 命名管道JSON通信协议
- Core/Service: Windows后台服务 + M1测试工具
- GUI/RemoteGameHub.UI: C# WPF 8 前端 (游戏库/设置/统计)
- DevEnvSetup.bat: Windows一键部署脚本
- BuildAll.bat/BuildSQLite.bat: 构建脚本
- QuickTest.bat: 快速验证脚本
- Windows部署指南.md: 完整中文文档

环境: Windows 10/11, Visual Studio 2022, WinFsp 2.0+, SQLite amalgamation
"
echo "--- step 5: set origin"
if git remote get-url origin > /dev/null 2>&1; then
  git remote set-url origin https://github.com/Chisleep/SMB_REMOTE_GAME_DISK.git
else
  git remote add origin https://github.com/Chisleep/SMB_REMOTE_GAME_DISK.git
fi
echo "--- step 6: branch"
BR=$(git rev-parse --abbrev-ref HEAD)
echo "current branch: $BR"
echo "--- step 7: push"
git push -u origin "$BR"
echo "--- done"
