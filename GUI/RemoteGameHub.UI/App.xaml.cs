using System;
using System.Threading;
using System.Windows;
using RemoteGameHub.UI.Services;

namespace RemoteGameHub.UI
{
    /// <summary>
    /// 应用程序入口，负责单实例控制与后台服务管道连接管理
    /// </summary>
    public partial class App : Application
    {
        // 单实例互斥锁
        private static Mutex? _singleInstanceMutex;
        // 全局管道客户端（与后台服务通信）
        public static PipeClientService PipeClient { get; private set; } = new PipeClientService();

        protected override void OnStartup(StartupEventArgs e)
        {
            // 单实例检测：若已有实例运行则退出
            bool createdNew;
            _singleInstanceMutex = new Mutex(true, "Global\\RemoteGameHub.UI.SingleInstance", out createdNew);
            if (!createdNew)
            {
                MessageBox.Show("RemoteGameHub 已经在运行中。", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
                Shutdown();
                return;
            }

            base.OnStartup(e);

            // 启动后台连接（不阻塞 UI）
            _ = PipeClient.ConnectAsync();
        }

        protected override void OnExit(ExitEventArgs e)
        {
            try
            {
                PipeClient.Disconnect();
            }
            catch
            {
                // 退出时忽略管道异常
            }
            _singleInstanceMutex?.ReleaseMutex();
            _singleInstanceMutex?.Dispose();
            base.OnExit(e);
        }
    }
}
