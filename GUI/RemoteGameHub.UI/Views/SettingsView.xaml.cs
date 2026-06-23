using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using RemoteGameHub.UI.Services;

namespace RemoteGameHub.UI.Views
{
    /// <summary>
    /// 设置视图：SMB 服务器与虚拟磁盘配置
    /// </summary>
    public partial class SettingsView : UserControl
    {
        private readonly GameLibraryService _library;

        public SettingsView()
        {
            InitializeComponent();
            _library = new GameLibraryService(App.PipeClient);
            Loaded += async (_, _) => await LoadConfigAsync();
        }

        /// <summary>加载已保存的配置</summary>
        private async System.Threading.Tasks.Task LoadConfigAsync()
        {
            try
            {
                var (smb, disk) = await _library.LoadConfigAsync();
                HostBox.Text = smb.Host;
                PortBox.Text = smb.Port.ToString();
                ShareBox.Text = smb.ShareName;
                DomainBox.Text = smb.Domain;
                UserBox.Text = smb.Username;
                PwdBox.Password = smb.Password;

                // 盘符选择
                if (!string.IsNullOrEmpty(disk.DriveLetter))
                {
                    int idx = char.ToUpper(disk.DriveLetter[0]) - 'A';
                    if (idx >= 0 && idx < DriveCombo.Items.Count)
                        DriveCombo.SelectedIndex = idx;
                }
                CacheBox.Text = disk.MemoryCacheSizeMB.ToString();
                ReadAheadCheck.IsChecked = disk.EnableReadAhead;
                ReadAheadBox.Text = disk.ReadAheadSizeKB.ToString();
                DirCacheBox.Text = disk.DirCacheTtlSec.ToString();
            }
            catch (Exception ex)
            {
                StatusTip.Text = "加载配置失败：" + ex.Message;
            }
        }

        /// <summary>测试 SMB 连接</summary>
        private async void OnTestConnectionClick(object sender, RoutedEventArgs e)
        {
            try
            {
                Mouse.OverrideCursor = Cursors.Wait;
                var cfg = BuildSmbConfig();
                var (ok, msg) = await _library.TestSmbAsync(cfg);
                StatusTip.Text = ok ? "✅ 连接成功：" + msg : "❌ 连接失败：" + msg;
            }
            catch (Exception ex)
            {
                StatusTip.Text = "❌ 测试失败：" + ex.Message;
            }
            finally
            {
                Mouse.OverrideCursor = null;
            }
        }

        /// <summary>保存配置</summary>
        private async void OnSaveClick(object sender, RoutedEventArgs e)
        {
            try
            {
                Mouse.OverrideCursor = Cursors.Wait;
                var smb = BuildSmbConfig();
                var disk = BuildDiskConfig();
                await _library.SaveConfigAsync(smb, disk);
                StatusTip.Text = "✅ 配置已保存";
            }
            catch (Exception ex)
            {
                StatusTip.Text = "❌ 保存失败：" + ex.Message;
            }
            finally
            {
                Mouse.OverrideCursor = null;
            }
        }

        /// <summary>重新加载配置</summary>
        private async void OnReloadClick(object sender, RoutedEventArgs e)
        {
            await LoadConfigAsync();
            StatusTip.Text = "已重新加载配置";
        }

        /// <summary>从表单构建 SMB 配置</summary>
        private SmbConfig BuildSmbConfig()
        {
            int port = int.TryParse(PortBox.Text, out var p) ? p : 445;
            return new SmbConfig
            {
                Host = HostBox.Text?.Trim() ?? string.Empty,
                Port = port,
                ShareName = ShareBox.Text?.Trim() ?? string.Empty,
                Domain = DomainBox.Text?.Trim() ?? string.Empty,
                Username = UserBox.Text?.Trim() ?? string.Empty,
                Password = PwdBox.Password ?? string.Empty
            };
        }

        /// <summary>从表单构建虚拟磁盘配置</summary>
        private VirtualDiskConfig BuildDiskConfig()
        {
            var item = DriveCombo.SelectedItem as ComboBoxItem;
            string letter = "G";
            if (item != null && item.Content is string s && s.Length >= 1)
                letter = s.Substring(0, 1);

            return new VirtualDiskConfig
            {
                DriveLetter = letter,
                MemoryCacheSizeMB = int.TryParse(CacheBox.Text, out var c) ? c : 512,
                EnableReadAhead = ReadAheadCheck.IsChecked ?? true,
                ReadAheadSizeKB = int.TryParse(ReadAheadBox.Text, out var r) ? r : 1024,
                DirCacheTtlSec = int.TryParse(DirCacheBox.Text, out var d) ? d : 30
            };
        }
    }
}
