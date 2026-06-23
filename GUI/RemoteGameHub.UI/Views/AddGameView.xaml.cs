using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media.Imaging;
using Microsoft.Win32;
using RemoteGameHub.UI.Models;
using RemoteGameHub.UI.Services;
using RemoteGameHub.UI.ViewModels;

namespace RemoteGameHub.UI.Views
{
    /// <summary>
    /// 添加游戏窗口：支持扫描 SMB 目录与手动填写表单
    /// </summary>
    public partial class AddGameView : Window
    {
        private readonly MainViewModel _mainVm;
        private readonly GameLibraryService _library;
        private readonly bool _startInScan;

        /// <summary>扫描结果集合</summary>
        public ObservableCollection<ScanResultItem> ScanResults { get; } = new();

        /// <summary>当前选中的封面路径</summary>
        private string _coverPath = string.Empty;

        public AddGameView(MainViewModel mainVm, bool startInScan = false)
        {
            InitializeComponent();
            _mainVm = mainVm;
            // 复用主窗口的库服务实例
            _library = new GameLibraryService(App.PipeClient);
            _startInScan = startInScan;

            ScanResultList.ItemsSource = ScanResults;
            ScanResultList.DataContext = this;

            if (startInScan)
            {
                TitleText.Text = "扫描 SMB 目录";
                // 自动触发一次扫描
                Loaded += async (_, _) => await DoScanAsync();
            }
        }

        /// <summary>点击扫描按钮</summary>
        private async void OnScanClick(object sender, RoutedEventArgs e)
        {
            await DoScanAsync();
        }

        /// <summary>执行扫描</summary>
        private async Task DoScanAsync()
        {
            string path = ScanPathBox.Text?.Trim() ?? string.Empty;
            if (string.IsNullOrEmpty(path) || path == "（SMB根目录）") path = string.Empty;

            try
            {
                Mouse.OverrideCursor = Cursors.Wait;
                ScanResults.Clear();
                var items = await _library.ScanGamesAsync(path);
                foreach (var it in items) ScanResults.Add(it);
                if (ScanResults.Count == 0)
                    MessageBox.Show("未扫描到游戏", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show("扫描失败：" + ex.Message, "错误", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                Mouse.OverrideCursor = null;
            }
        }

        /// <summary>扫描结果选中：自动填充表单</summary>
        private void OnScanSelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
        {
            if (ScanResultList.SelectedItem is ScanResultItem item)
            {
                NameBox.Text = item.Name;
                SmbPathBox.Text = item.SmbPath;
                ExePathBox.Text = item.ExeRelativePath;
                if (!string.IsNullOrEmpty(item.CoverImagePath))
                {
                    _coverPath = item.CoverImagePath;
                    UpdateCoverPreview();
                }
            }
        }

        /// <summary>选择封面文件</summary>
        private void OnPickCoverClick(object sender, RoutedEventArgs e)
        {
            var dlg = new OpenFileDialog
            {
                Title = "选择封面图片",
                Filter = "图片文件|*.png;*.jpg;*.jpeg;*.bmp;*.gif|所有文件|*.*"
            };
            if (dlg.ShowDialog() == true)
            {
                _coverPath = dlg.FileName;
                UpdateCoverPreview();
            }
        }

        /// <summary>更新封面预览</summary>
        private void UpdateCoverPreview()
        {
            try
            {
                if (!string.IsNullOrEmpty(_coverPath) && File.Exists(_coverPath))
                {
                    CoverPreview.Source = new BitmapImage(new Uri(_coverPath));
                }
                else
                {
                    CoverPreview.Source = null;
                }
            }
            catch
            {
                CoverPreview.Source = null;
            }
        }

        /// <summary>保存游戏</summary>
        private async void OnSaveClick(object sender, RoutedEventArgs e)
        {
            // 校验必填项
            if (string.IsNullOrWhiteSpace(NameBox.Text))
            {
                MessageBox.Show("请填写游戏名称", "提示", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            if (string.IsNullOrWhiteSpace(ExePathBox.Text))
            {
                MessageBox.Show("请填写 exe 相对路径", "提示", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            var game = new GameModel
            {
                Name = NameBox.Text.Trim(),
                SmbPath = SmbPathBox.Text?.Trim() ?? string.Empty,
                ExeRelativePath = ExePathBox.Text.Trim(),
                LaunchArgs = ArgsBox.Text?.Trim() ?? string.Empty,
                CoverImagePath = _coverPath,
                Compatibility = (GameCompatibility)CompatCombo.SelectedIndex,
                Tags = (TagsBox.Text ?? string.Empty)
                    .Split(',', '，', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
                    .ToList(),
                AddedDate = DateTimeOffset.UtcNow.ToUnixTimeSeconds()
            };

            try
            {
                Mouse.OverrideCursor = Cursors.Wait;
                await _library.AddGameAsync(game);
                MessageBox.Show("游戏添加成功", "成功", MessageBoxButton.OK, MessageBoxImage.Information);
                // 通知主窗口刷新
                await _mainVm.RefreshAsync();
                DialogResult = true;
                Close();
            }
            catch (Exception ex)
            {
                MessageBox.Show("保存失败：" + ex.Message, "错误", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                Mouse.OverrideCursor = null;
            }
        }

        /// <summary>取消</summary>
        private void OnCancelClick(object sender, RoutedEventArgs e)
        {
            DialogResult = false;
            Close();
        }
    }
}
