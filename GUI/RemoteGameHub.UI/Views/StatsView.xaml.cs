using System;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using RemoteGameHub.UI.Models;
using RemoteGameHub.UI.Services;

namespace RemoteGameHub.UI.Views
{
    /// <summary>
    /// 统计视图：展示总游玩时长、启动次数排行、最近游玩
    /// </summary>
    public partial class StatsView : UserControl
    {
        private readonly GameLibraryService _library;

        public StatsView()
        {
            InitializeComponent();
            _library = new GameLibraryService(App.PipeClient);
            Loaded += async (_, _) => await LoadAsync();
        }

        /// <summary>加载统计数据</summary>
        public async System.Threading.Tasks.Task LoadAsync()
        {
            try
            {
                var stats = await _library.GetStatsAsync();
                TotalGamesText.Text = stats.TotalGames.ToString();
                TotalPlayCountText.Text = stats.TotalPlayCount.ToString();
                TotalPlayTimeText.Text = GameModel.FormatPlayTime(stats.TotalPlayTimeSec);

                TopPlayedList.ItemsSource = stats.TopPlayed;
                RecentPlayedList.ItemsSource = stats.RecentPlayed;
            }
            catch (Exception ex)
            {
                TotalGamesText.Text = "--";
                TotalPlayCountText.Text = "--";
                TotalPlayTimeText.Text = "--";
                System.Diagnostics.Debug.WriteLine("加载统计失败：" + ex.Message);
            }
        }
    }
}
