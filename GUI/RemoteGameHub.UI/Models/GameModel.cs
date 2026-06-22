using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Text.Json.Serialization;

namespace RemoteGameHub.UI.Models
{
    /// <summary>
    /// 游戏兼容性等级
    /// </summary>
    public enum GameCompatibility
    {
        // 未知
        Unknown = 0,
        // 完美运行
        Perfect = 1,
        // 实验性，可能有问题
        Experimental = 2,
        // 不兼容（如反作弊拦截）
        Incompatible = 3
    }

    /// <summary>
    /// 游戏数据模型，与后台 GameInfo 对应
    /// </summary>
    public class GameModel : INotifyPropertyChanged
    {
        private long _id;
        private string _name = string.Empty;
        private string _smbPath = string.Empty;
        private string _exeRelativePath = string.Empty;
        private string _launchArgs = string.Empty;
        private string _coverImagePath = string.Empty;
        private List<string> _tags = new();
        private GameCompatibility _compatibility = GameCompatibility.Unknown;
        private int _playCount;
        private long _totalPlayTimeSec;
        private long _lastPlayed;
        private long _addedDate;
        private bool _isFavorite;

        /// <summary>游戏唯一ID</summary>
        public long Id
        {
            get => _id;
            set => SetField(ref _id, value);
        }

        /// <summary>游戏名称</summary>
        public string Name
        {
            get => _name;
            set => SetField(ref _name, value);
        }

        /// <summary>SMB上的相对路径，如 "ActionGame"</summary>
        public string SmbPath
        {
            get => _smbPath;
            set => SetField(ref _smbPath, value);
        }

        /// <summary>相对exe路径，如 "ActionGame/game.exe"</summary>
        public string ExeRelativePath
        {
            get => _exeRelativePath;
            set => SetField(ref _exeRelativePath, value);
        }

        /// <summary>启动参数</summary>
        public string LaunchArgs
        {
            get => _launchArgs;
            set => SetField(ref _launchArgs, value);
        }

        /// <summary>封面图本地缓存路径</summary>
        public string CoverImagePath
        {
            get => _coverImagePath;
            set => SetField(ref _coverImagePath, value);
        }

        /// <summary>标签列表</summary>
        public List<string> Tags
        {
            get => _tags;
            set => SetField(ref _tags, value);
        }

        /// <summary>兼容性等级</summary>
        public GameCompatibility Compatibility
        {
            get => _compatibility;
            set => SetField(ref _compatibility, value);
        }

        /// <summary>启动次数</summary>
        public int PlayCount
        {
            get => _playCount;
            set => SetField(ref _playCount, value);
        }

        /// <summary>总游玩时长（秒）</summary>
        public long TotalPlayTimeSec
        {
            get => _totalPlayTimeSec;
            set => SetField(ref _totalPlayTimeSec, value);
        }

        /// <summary>上次游玩时间（Unix时间戳，秒）</summary>
        public long LastPlayed
        {
            get => _lastPlayed;
            set => SetField(ref _lastPlayed, value);
        }

        /// <summary>添加日期（Unix时间戳，秒）</summary>
        public long AddedDate
        {
            get => _addedDate;
            set => SetField(ref _addedDate, value);
        }

        /// <summary>是否收藏（仅本地UI状态）</summary>
        [JsonIgnore]
        public bool IsFavorite
        {
            get => _isFavorite;
            set => SetField(ref _isFavorite, value);
        }

        /// <summary>总游玩时长的可读字符串</summary>
        [JsonIgnore]
        public string PlayTimeDisplay => FormatPlayTime(TotalPlayTimeSec);

        /// <summary>上次游玩时间的可读字符串</summary>
        [JsonIgnore]
        public string LastPlayedDisplay => LastPlayed > 0
            ? DateTimeOffset.FromUnixTimeSeconds(LastPlayed).LocalDateTime.ToString("yyyy-MM-dd HH:mm")
            : "从未";

        /// <summary>兼容性显示文本</summary>
        [JsonIgnore]
        public string CompatibilityDisplay => Compatibility switch
        {
            GameCompatibility.Perfect => "完美",
            GameCompatibility.Experimental => "实验性",
            GameCompatibility.Incompatible => "不兼容",
            _ => "未知"
        };

        /// <summary>标签拼接字符串</summary>
        [JsonIgnore]
        public string TagsDisplay => Tags.Count > 0 ? string.Join(" / ", Tags) : "无标签";

        /// <summary>
        /// 将秒数格式化为可读时长
        /// </summary>
        public static string FormatPlayTime(long seconds)
        {
            if (seconds <= 0) return "0分钟";
            var ts = TimeSpan.FromSeconds(seconds);
            if (ts.TotalHours >= 1) return $"{(int)ts.TotalHours}小时{ts.Minutes}分";
            if (ts.TotalMinutes >= 1) return $"{(int)ts.TotalMinutes}分钟";
            return $"{ts.Seconds}秒";
        }

        public event PropertyChangedEventHandler? PropertyChanged;

        protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
            => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

        protected bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
        {
            if (EqualityComparer<T>.Default.Equals(field, value)) return false;
            field = value;
            OnPropertyChanged(propertyName);
            // 派生属性同步刷新
            if (propertyName == nameof(TotalPlayTimeSec)) OnPropertyChanged(nameof(PlayTimeDisplay));
            if (propertyName == nameof(LastPlayed)) OnPropertyChanged(nameof(LastPlayedDisplay));
            if (propertyName == nameof(Compatibility)) OnPropertyChanged(nameof(CompatibilityDisplay));
            if (propertyName == nameof(Tags)) OnPropertyChanged(nameof(TagsDisplay));
            return true;
        }
    }
}
