using System;
using System.Windows;
using RemoteGameHub.UI.Services;
using RemoteGameHub.UI.ViewModels;

namespace RemoteGameHub.UI.Views
{
    /// <summary>
    /// 主窗口交互逻辑
    /// </summary>
    public partial class MainWindow : Window
    {
        private readonly MainViewModel _viewModel;

        public MainWindow()
        {
            InitializeComponent();

            // 构建服务与视图模型
            var pipe = App.PipeClient;
            var library = new GameLibraryService(pipe);
            _viewModel = new MainViewModel(pipe, library);
            DataContext = _viewModel;

            // 注册 UI 交互回调
            _viewModel.RequestAddGame = OpenAddGame;
            _viewModel.RequestScan = OpenScan;
            _viewModel.LaunchProgress = OnLaunchProgress;
            _viewModel.LaunchFinished = OnLaunchFinished;
            _viewModel.NavChanged = OnNavChanged;

            // 加载子视图
            StatsHost.Content = new StatsView { DataContext = _viewModel };
            SettingsHost.Content = new SettingsView();
        }

        /// <summary>打开添加游戏窗口</summary>
        private void OpenAddGame()
        {
            var win = new AddGameView(_viewModel)
            {
                Owner = this
            };
            win.ShowDialog();
        }

        /// <summary>打开扫描窗口（复用 AddGameView 的扫描功能）</summary>
        private void OpenScan()
        {
            var win = new AddGameView(_viewModel, startInScan: true)
            {
                Owner = this
            };
            win.ShowDialog();
        }

        /// <summary>启动进度回调</summary>
        private void OnLaunchProgress(string status, string message)
        {
            // 可在此显示进度条或 Toast，这里仅更新状态栏（ViewModel 已处理）
        }

        /// <summary>启动结束回调</summary>
        private void OnLaunchFinished(bool ok, string message)
        {
            if (!ok && !string.IsNullOrEmpty(message))
            {
                MessageBox.Show(message, "启动失败", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        }

        /// <summary>导航变化：切换子视图可见性</summary>
        private void OnNavChanged(NavCategory nav)
        {
            // 子视图可见性已通过 XAML DataTrigger 控制，此处可扩展额外逻辑
        }
    }
}
