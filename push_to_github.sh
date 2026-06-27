#!/bin/bash
# 一键把 /workspace 提交到 https://github.com/Chisleep/SMB_REMOTE_GAME_DISK
set -e
cd /workspace

echo "=== 设置 git 用户"
git config user.name "Chisleep"
git config user.email "chisleep@users.noreply.github.com"

echo "=== 添加文件 (排除 .git 目录外所有内容"
git add -A

echo "=== git add 之后的改动"
git status --short | head -40

echo ""
echo "=== 提交"
git commit -m "init: RemoteGameHub - WinFsp虚拟盘 + SMB客户端 + 游戏库 + WPF GUI + 部署脚本

- Core/ ：C++核心：日志/配置/凭据
- Core/SMB：SMB客户端+连接池
- Core/VirtualDisk：WinFsp 虚拟盘 + 内存缓存（LRU/目录缓存）
- Core/GameLibrary：扫描器、封面抓取、SQLite 数据库
- Core/Launcher：启动管理/进程监控
- Core/Stats：游玩统计记录
- Core/IPC：命名管道通信/JSON协议
- Core/Service：MainService：主服务/Windows服务入口/虚拟盘测试工具
- GUI/RemoteGameHub.UI：C# WPF 前端（游戏库窗口/添加/设置
- DevEnvSetup.bat：Windows一键部署脚本
- BuildAll.bat/BuildSQLite.bat：一键编译
- QuickTest.bat 交互式挂载测试
- Windows部署指南.md：完整文档

支持：Windows 10/11 + Visual Studio 2022 + WinFsp 2.0 + SQLite amalgamation + .NET 8 + SQLite amalgamation" 2>&1 | tail -3

echo ""
echo "=== 设置 remote origin"
if git remote get-url origin >/dev/null 2>&1; then
  echo "  已存在 origin，改为你的仓库"
  git remote set-url origin https://github.com/Chisleep/SMB_REMOTE_GAME_DISK.git
else
  echo "  添加 origin=https://github.com/Chisleep/SMB_REMOTE_GAME_DISK.git
fi

echo ""
echo "=== 远端"
git remote -v

echo ""
echo "=== 推送到 GitHub (git push -u origin main"
git branch --show-current"
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo "当前分支: $CURRENT_BRANCH"

echo ""
echo "=== 执行 git push"
if git push -u origin "$CURRENT_BRANCH"; then
  echo ""
  echo "=== ✅ 推送成功!"
  echo "你可在 Windows 端执行:"
  echo "  git clone https://github.com/Chisleep/SMB_REMOTE_GAME_DISK.git C:\\RemoteGameHub"
  echo "  cd /d C:\\RemoteGameHub"
  echo "  DevEnvSetup.bat   (管理员身份)
  echo ""
else
  echo ""
  echo "❌ 推送失败，可能原因：
  1. GitHub 需要 Personal Access Token (PAT)
  2. 仓库尚未在 GitHub 上创建
  3. SSH key 未配置"
  echo ""
  echo "=== 请在 Windows 端手动推送 (或让我配置 token"
  echo ""
  echo "=== 备用方案：
  echo "  在 Windows 端：
  echo "    cd /d C:\RemoteGameHub"
  echo "    git init"
  echo "    git add .
  echo "    git commit -m init"
  echo "    git remote add origin https://github.com/Chisleep/SMB_REMOTE_GAME_DISK.git"
  echo "    git push -u origin main (或 main
  echo ""
fi
