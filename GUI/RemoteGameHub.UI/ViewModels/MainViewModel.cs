using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using RemoteGameHub.UI.Models;
using RemoteGameHub.UI.Services;

namespace RemoteGameHub.UI.ViewModels
{
    /// <summary>
    /// 导航分类类型
    /// </summary>
    public enum NavCategory
    {
        AllGames,
        Favorites,
        ByTag,
        Stats,
        Settings
    }

    /// <summary>
    /// 主视图模型：管理游戏列表、连接状态与命令
    /// </summary>
    public class MainViewModel : INotifyPropertyChanged
    {
        private readonly GameLibraryService _library;
        private readonly PipeClientService _pipe;

        private GameModel? _selectedGame;
        private bool _isConnected;
        private string _statusText = "正在连接后台服务...";
        private string _latencyText = "--";
        private NavCategory _currentNav = NavCategory.AllGames;
        private string _searchText = string.Empty;
        private string _selectedTag = string.Empty;
        private bool _isBusy;

        public MainViewModel(PipeClientService pipe, GameLibraryService library)
        {
            _pipe = pipe;
            _library = library;

            // 订阅连接状态与事件
            _pipe.ConnectionChanged += OnConnectionChanged;
            _pipe.OnEventReceived += OnEventReceived;

            Games = new ObservableCollection<GameModel>();
            AllTags = new ObservableCollection<string>();
            FilteredGames = new ObservableCollection<GameModel>();

            LaunchGameCommand = new RelayCommand<GameModel?>(g => _ = LaunchGameAsync(g), g => g != null && IsConnected);
            AddGameCommand = new RelayCommand(() => RequestAddGame?.Invoke(), () => IsConnected);
            RemoveGameCommand = new RelayCommand<GameModel?>(g => _ = RemoveGameAsync(g), g => g != null && IsConnected);
            RefreshCommand = new RelayCommand(() => _ = RefreshAsync(), () => IsConnected);
            ScanCommand = new RelayCommand(() => RequestScan?.Invoke(), () => IsConnected);
            ToggleFavoriteCommand = new RelayCommand<GameModel?>(g => ToggleFavorite(g), g => g != null);
            SelectNavCommand = new RelayCommand<NavCategory>(n => CurrentNav = n);
        }

        // ===== 属性 =====

        /// <summary>全部游戏</summary>
        public ObservableCollection<GameModel> Games { get; }

        /// <summary>过滤后展示的游戏</summary>
        public ObservableCollection<GameModel> FilteredGames { get; }

        /// <summary>所有标签</summary>
        public ObservableCollection<string> AllTags { get; }

        public GameModel? SelectedGame
        {
            get => _selectedGame;
            set { if (SetField(ref _selectedGame, value)) { } }
        }

        public bool IsConnected
        {
            get => _isConnected;
            set
            {
                if (SetField(ref _isConnected, value))
                {
                    StatusText = value ? "已连接后台服务" : "未连接后台服务";
                    (LaunchGameCommand as RelayCommand)?.RaiseCanExecuteChanged();
                    (AddGameCommand as RelayCommand)?.RaiseCanExecuteChanged();
                    (RemoveGameCommand as RelayCommand)?.RaiseCanExecuteChanged();
                    (RefreshCommand as RelayCommand)?.RaiseCanExecuteChanged();
                    (ScanCommand as RelayCommand)?.RaiseCanExecuteChanged();
                }
            }
        }

        public string StatusText
        {
            get => _statusText;
            set => SetField(ref _statusText, value);
        }

        public string LatencyText
        {
            get => _latencyText;
            set => SetField(ref _latencyText, value);
        }

        public NavCategory CurrentNav
        {
            get => _currentNav;
            set
            {
                if (SetField(ref _currentNav, value))
                {
                    ApplyFilter();
                    NavChanged?.Invoke(value);
                }
            }
        }

        public string SearchText
        {
            get => _searchText;
            set { if (SetField(ref _searchText, value)) ApplyFilter(); }
        }

        public string SelectedTag
        {
            get => _selectedTag;
            set { if (SetField(ref _selectedTag, value)) ApplyFilter(); }
        }

        public bool IsBusy
        {
            get => _isBusy;
            set => SetField(ref _isBusy, value);
        }

        // ===== 命令 =====

        public ICommand LaunchGameCommand { get; }
        public ICommand AddGameCommand { get; }
        public ICommand RemoveGameCommand { get; }
        public ICommand RefreshCommand { get; }
        public ICommand ScanCommand { get; }
        public ICommand ToggleFavoriteCommand { get; }
        public ICommand SelectNavCommand { get; }

        // ===== UI 交互回调（由 View 注册） =====

        /// <summary>请求打开添加游戏窗口</summary>
        public Action? RequestAddGame { get; set; }
        /// <summary>请求打开扫描窗口</summary>
        public Action? RequestScan { get; set; }
        /// <summary>导航变化通知</summary>
        public Action<NavCategory>? NavChanged { get; set; }
        /// <summary>启动进度通知（status, message）</summary>
        public Action<string, string>? LaunchProgress { get; set; }
        /// <summary>启动完成通知</summary>
        public Action<bool, string>? LaunchFinished { get; set; }

        // ===== 方法 =====

        /// <summary>加载游戏列表</summary>
        public async Task RefreshAsync()
        {
            if (!IsConnected) return;
            IsBusy = true;
            try
            {
                var list = await _library.GetGameListAsync();
                Games.Clear();
                foreach (var g in list) Games.Add(g);
                RebuildTags();
                ApplyFilter();
                StatusText = $"已加载 {list.Count} 个游戏";
            }
            catch (Exception ex)
            {
                StatusText = "加载失败：" + ex.Message;
            }
            finally
            {
                IsBusy = false;
            }
        }

        /// <summary>启动游戏</summary>
        public async Task LaunchGameAsync(GameModel? game)
        {
            if (game == null || !IsConnected) return;
            try
            {
                StatusText = $"正在启动：{game.Name}";
                await _library.LaunchGameAsync(game.Id);
            }
            catch (Exception ex)
            {
                StatusText = "启动失败：" + ex.Message;
                LaunchFinished?.Invoke(false, ex.Message);
            }
        }

        /// <summary>删除游戏</summary>
        public async Task RemoveGameAsync(GameModel? game)
        {
            if (game == null || !IsConnected) return;
            var ok = MessageBox.Show($"确认删除游戏「{game.Name}」？", "确认删除",
                MessageBoxButton.YesNo, MessageBoxImage.Question);
            if (ok != MessageBoxResult.Yes) return;

            try
            {
                await _library.RemoveGameAsync(game.Id);
                Games.Remove(game);
                FilteredGames.Remove(game);
                StatusText = $"已删除：{game.Name}";
            }
            catch (Exception ex)
            {
                StatusText = "删除失败：" + ex.Message;
            }
        }

        /// <summary>切换收藏</summary>
        public void ToggleFavorite(GameModel? game)
        {
            if (game == null) return;
            game.IsFavorite = !game.IsFavorite;
            if (CurrentNav == NavCategory.Favorites) ApplyFilter();
        }

        /// <summary>应用过滤与分类</summary>
        public void ApplyFilter()
        {
            FilteredGames.Clear();
            var query = Games.AsEnumerable();

            // 分类过滤
            switch (CurrentNav)
            {
                case NavCategory.Favorites:
                    query = query.Where(g => g.IsFavorite);
                    break;
                case NavCategory.ByTag:
                    if (!string.IsNullOrEmpty(SelectedTag))
                        query = query.Where(g => g.Tags.Contains(SelectedTag));
                    break;
            }

            // 搜索过滤
            if (!string.IsNullOrWhiteSpace(SearchText))
            {
                var kw = SearchText.Trim();
                query = query.Where(g => g.Name.Contains(kw, StringComparison.OrdinalIgnoreCase));
            }

            foreach (var g in query) FilteredGames.Add(g);
        }

        /// <summary>重建标签集合</summary>
        private void RebuildTags()
        {
            AllTags.Clear();
            var tags = Games.SelectMany(g => g.Tags).Distinct().OrderBy(t => t);
            foreach (var t in tags) AllTags.Add(t);
        }

        // ===== 事件处理 =====

        private void OnConnectionChanged(object? sender, bool connected)
        {
            // 切回 UI 线程
            Application.Current?.Dispatcher.Invoke(() =>
            {
                IsConnected = connected;
                if (connected)
                {
                    StatusText = "已连接后台服务";
                    _ = RefreshAsync();
                    _ = MeasureLatencyAsync();
                }
                else
                {
                    StatusText = "未连接后台服务（将自动重连）";
                    LatencyText = "--";
                    _ = TryReconnectAsync();
                }
            });
        }

        private void OnEventReceived(object? sender, IpcEventArgs e)
        {
            Application.Current?.Dispatcher.Invoke(() =>
            {
                switch (e.Action)
                {
                    case "launch_progress":
                        LaunchProgress?.Invoke(
                            e.Params.TryGetValue("status", out var s) ? s : "",
                            e.Message);
                        StatusText = e.Message;
                        break;
                    case "launch_complete":
                        LaunchFinished?.Invoke(true, e.Message);
                        StatusText = "游戏已启动";
                        _ = RefreshAsync();
                        break;
                    case "launch_failed":
                        LaunchFinished?.Invoke(false, e.Message);
                        StatusText = "启动失败：" + e.Message;
                        break;
                    case "game_exited":
                        StatusText = "游戏已退出";
                        _ = RefreshAsync();
                        break;
                    case "scan_progress":
                        StatusText = "扫描中：" + e.Message;
                        break;
                    case "scan_complete":
                        StatusText = "扫描完成";
                        _ = RefreshAsync();
                        break;
                }
            });
        }

        /// <summary>测量延迟</summary>
        private async Task MeasureLatencyAsync()
        {
            try
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                await _library.PingAsync();
                sw.Stop();
                LatencyText = $"{sw.ElapsedMilliseconds} ms";
            }
            catch
            {
                LatencyText = "--";
            }
        }

        /// <summary>断线重连</summary>
        private async Task TryReconnectAsync()
        {
            for (int i = 0; i < 10 && !IsConnected; i++)
            {
                await Task.Delay(3000);
                try { await _pipe.ConnectAsync(); }
                catch { /* 继续重试 */ }
            }
        }

        // ===== INotifyPropertyChanged =====

        public event PropertyChangedEventHandler? PropertyChanged;

        protected void OnPropertyChanged([CallerMemberName] string? name = null)
            => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

        protected bool SetField<T>(ref T field, T value, [CallerMemberName] string? name = null)
        {
            if (EqualityComparer<T>.Default.Equals(field, value)) return false;
            field = value;
            OnPropertyChanged(name);
            return true;
        }
    }

    /// <summary>带强类型参数的 RelayCommand 简化封装</summary>
    public class RelayCommand<T> : ICommand
    {
        private readonly Action<T?> _execute;
        private readonly Func<T?, bool>? _canExecute;

        public RelayCommand(Action<T?> execute, Func<T?, bool>? canExecute = null)
        {
            _execute = execute;
            _canExecute = canExecute;
        }

        public bool CanExecute(object? parameter)
        {
            if (parameter is T t) return _canExecute?.Invoke(t) ?? true;
            if (parameter == null && default(T) == null) return _canExecute?.Invoke(default) ?? true;
            return false;
        }

        public void Execute(object? parameter)
        {
            if (parameter is T t) _execute(t);
            else if (parameter == null) _execute(default);
        }

        public event EventHandler? CanExecuteChanged
        {
            add { CommandManager.RequerySuggested += value; }
            remove { CommandManager.RequerySuggested -= value; }
        }

        public void RaiseCanExecuteChanged() => CommandManager.InvalidateRequerySuggested();
    }
}
