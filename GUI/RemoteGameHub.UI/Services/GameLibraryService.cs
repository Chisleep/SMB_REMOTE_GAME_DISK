using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;
using RemoteGameHub.UI.Models;

namespace RemoteGameHub.UI.Services
{
    /// <summary>
    /// 扫描结果项：扫描 SMB 目录得到的候选游戏
    /// </summary>
    public class ScanResultItem
    {
        public string Name { get; set; } = string.Empty;
        public string SmbPath { get; set; } = string.Empty;
        public string ExeRelativePath { get; set; } = string.Empty;
        public string CoverImagePath { get; set; } = string.Empty;
        public bool HasCover { get; set; }
    }

    /// <summary>
    /// 统计数据
    /// </summary>
    public class LibraryStats
    {
        public int TotalGames { get; set; }
        public int TotalPlayCount { get; set; }
        public long TotalPlayTimeSec { get; set; }
        public List<GameModel> TopPlayed { get; set; } = new();
        public List<GameModel> RecentPlayed { get; set; } = new();
    }

    /// <summary>
    /// SMB 服务器配置（字段名与后台 C++ SmbServerConfig 对应）
    /// </summary>
    public class SmbConfig
    {
        [JsonPropertyName("host")] public string Host { get; set; } = string.Empty;
        [JsonPropertyName("port")] public int Port { get; set; } = 445;
        [JsonPropertyName("shareName")] public string ShareName { get; set; } = string.Empty;
        [JsonPropertyName("username")] public string Username { get; set; } = string.Empty;
        [JsonPropertyName("password")] public string Password { get; set; } = string.Empty;
        [JsonPropertyName("domain")] public string Domain { get; set; } = string.Empty;
    }

    /// <summary>
    /// 虚拟磁盘配置（字段名与后台 C++ VirtualDiskConfig 对应）
    /// </summary>
    public class VirtualDiskConfig
    {
        [JsonPropertyName("driveLetter")] public string DriveLetter { get; set; } = "G";
        [JsonPropertyName("memoryCacheSizeMB")] public int MemoryCacheSizeMB { get; set; } = 512;
        [JsonPropertyName("enableReadAhead")] public bool EnableReadAhead { get; set; } = true;
        [JsonPropertyName("readAheadSizeKB")] public int ReadAheadSizeKB { get; set; } = 1024;
        [JsonPropertyName("dirCacheTtlSec")] public int DirCacheTtlSec { get; set; } = 30;
    }

    /// <summary>
    /// 游戏库服务：封装与后台服务的游戏库相关调用
    /// </summary>
    public class GameLibraryService
    {
        private readonly PipeClientService _pipe;

        public GameLibraryService(PipeClientService pipe)
        {
            _pipe = pipe;
        }

        /// <summary>
        /// 测试连接（ping）
        /// </summary>
        public async Task<bool> PingAsync()
        {
            try
            {
                var resp = await _pipe.SendRequestAsync("ping");
                return resp.Success;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// 扫描 SMB 目录下的游戏
        /// </summary>
        /// <param name="smbPath">SMB 相对路径</param>
        public async Task<List<ScanResultItem>> ScanGamesAsync(string smbPath)
        {
            var @params = new Dictionary<string, string> { ["smbPath"] = smbPath };
            var resp = await _pipe.SendRequestAsync("scan_games", @params);
            EnsureSuccess(resp);

            var list = new List<ScanResultItem>();
            if (resp.Data.HasValue && resp.Data.Value.ValueKind == JsonValueKind.Array)
            {
                foreach (var item in resp.Data.Value.EnumerateArray())
                {
                    list.Add(ParseScanItem(item));
                }
            }
            return list;
        }

        /// <summary>
        /// 获取游戏列表
        /// </summary>
        public async Task<List<GameModel>> GetGameListAsync()
        {
            var resp = await _pipe.SendRequestAsync("get_game_list");
            EnsureSuccess(resp);

            var list = new List<GameModel>();
            if (resp.Data.HasValue && resp.Data.Value.ValueKind == JsonValueKind.Array)
            {
                foreach (var item in resp.Data.Value.EnumerateArray())
                {
                    list.Add(ParseGame(item));
                }
            }
            return list;
        }

        /// <summary>
        /// 添加游戏
        /// </summary>
        public async Task<GameModel> AddGameAsync(GameModel game)
        {
            var resp = await _pipe.SendRequestAsync("add_game", jsonData: SerializeGame(game));
            EnsureSuccess(resp);
            // 返回的 data 可能包含新分配的 id
            if (resp.Data.HasValue && resp.Data.Value.ValueKind == JsonValueKind.Object)
            {
                return ParseGame(resp.Data.Value);
            }
            return game;
        }

        /// <summary>
        /// 删除游戏
        /// </summary>
        public async Task RemoveGameAsync(long gameId)
        {
            var @params = new Dictionary<string, string> { ["gameId"] = gameId.ToString() };
            var resp = await _pipe.SendRequestAsync("remove_game", @params);
            EnsureSuccess(resp);
        }

        /// <summary>
        /// 更新游戏信息
        /// </summary>
        public async Task UpdateGameAsync(GameModel game)
        {
            var @params = new Dictionary<string, string> { ["gameId"] = game.Id.ToString() };
            var resp = await _pipe.SendRequestAsync("update_game", @params, SerializeGame(game));
            EnsureSuccess(resp);
        }

        /// <summary>
        /// 启动游戏
        /// </summary>
        public async Task LaunchGameAsync(long gameId)
        {
            var @params = new Dictionary<string, string> { ["gameId"] = gameId.ToString() };
            var resp = await _pipe.SendRequestAsync("launch_game", @params);
            EnsureSuccess(resp);
        }

        /// <summary>
        /// 终止游戏
        /// </summary>
        public async Task TerminateGameAsync(long gameId)
        {
            var @params = new Dictionary<string, string> { ["gameId"] = gameId.ToString() };
            var resp = await _pipe.SendRequestAsync("terminate_game", @params);
            EnsureSuccess(resp);
        }

        /// <summary>
        /// 获取统计数据
        /// </summary>
        public async Task<LibraryStats> GetStatsAsync()
        {
            var resp = await _pipe.SendRequestAsync("get_stats");
            EnsureSuccess(resp);

            var stats = new LibraryStats();
            if (resp.Data.HasValue && resp.Data.Value.ValueKind == JsonValueKind.Object)
            {
                var d = resp.Data.Value;
                stats.TotalGames = d.TryGetProperty("totalGames", out var tg) ? tg.GetInt32() : 0;
                stats.TotalPlayCount = d.TryGetProperty("totalPlayCount", out var tp) ? tp.GetInt32() : 0;
                stats.TotalPlayTimeSec = d.TryGetProperty("totalPlayTimeSec", out var tt) ? tt.GetInt64() : 0;
                if (d.TryGetProperty("topPlayed", out var top) && top.ValueKind == JsonValueKind.Array)
                    foreach (var g in top.EnumerateArray()) stats.TopPlayed.Add(ParseGame(g));
                if (d.TryGetProperty("recentPlayed", out var rec) && rec.ValueKind == JsonValueKind.Array)
                    foreach (var g in rec.EnumerateArray()) stats.RecentPlayed.Add(ParseGame(g));
            }
            return stats;
        }

        /// <summary>
        /// 测试 SMB 连接
        /// </summary>
        public async Task<(bool ok, string message)> TestSmbAsync(SmbConfig cfg)
        {
            var resp = await _pipe.SendRequestAsync("test_smb", jsonData: JsonSerializer.Serialize(cfg));
            bool ok = resp.Success;
            string msg = resp.Params.TryGetValue("message", out var m) ? m : resp.Error;
            return (ok, msg);
        }

        /// <summary>
        /// 保存配置
        /// </summary>
        public async Task SaveConfigAsync(SmbConfig smb, VirtualDiskConfig disk)
        {
            var payload = JsonSerializer.Serialize(new { smb, disk });
            var resp = await _pipe.SendRequestAsync("save_config", jsonData: payload);
            EnsureSuccess(resp);
        }

        /// <summary>
        /// 加载配置
        /// </summary>
        public async Task<(SmbConfig smb, VirtualDiskConfig disk)> LoadConfigAsync()
        {
            var resp = await _pipe.SendRequestAsync("load_config");
            EnsureSuccess(resp);
            SmbConfig smb = new();
            VirtualDiskConfig disk = new();
            if (resp.Data.HasValue && resp.Data.Value.ValueKind == JsonValueKind.Object)
            {
                var d = resp.Data.Value;
                if (d.TryGetProperty("smb", out var sEl) && sEl.ValueKind == JsonValueKind.Object)
                    smb = sEl.Deserialize<SmbConfig>(JsonOpts) ?? smb;
                if (d.TryGetProperty("disk", out var dEl) && dEl.ValueKind == JsonValueKind.Object)
                    disk = dEl.Deserialize<VirtualDiskConfig>(JsonOpts) ?? disk;
            }
            return (smb, disk);
        }

        // ===== 内部辅助 =====

        /// <summary>反序列化选项：属性名大小写不敏感（兼容后台不同命名风格）</summary>
        private static readonly JsonSerializerOptions JsonOpts = new()
        {
            PropertyNameCaseInsensitive = true
        };

        private static void EnsureSuccess(IpcResponse resp)
        {
            if (!resp.Success)
                throw new InvalidOperationException(string.IsNullOrEmpty(resp.Error) ? "操作失败" : resp.Error);
        }

        /// <summary>序列化 GameModel 为 JSON 文本</summary>
        private static string SerializeGame(GameModel g)
        {
            return JsonSerializer.Serialize(new
            {
                id = g.Id,
                name = g.Name,
                smbPath = g.SmbPath,
                exeRelativePath = g.ExeRelativePath,
                launchArgs = g.LaunchArgs,
                coverImagePath = g.CoverImagePath,
                tags = g.Tags,
                compatibility = (int)g.Compatibility,
                playCount = g.PlayCount,
                totalPlayTimeSec = g.TotalPlayTimeSec,
                lastPlayed = g.LastPlayed,
                addedDate = g.AddedDate
            });
        }

        /// <summary>从 JsonElement 解析 GameModel</summary>
        private static GameModel ParseGame(JsonElement el)
        {
            var g = new GameModel();
            if (el.ValueKind != JsonValueKind.Object) return g;
            g.Id = el.TryGetProperty("id", out var id) ? id.GetInt64() : 0;
            g.Name = el.TryGetProperty("name", out var n) ? n.GetString() ?? "" : "";
            g.SmbPath = el.TryGetProperty("smbPath", out var sp) ? sp.GetString() ?? "" : "";
            g.ExeRelativePath = el.TryGetProperty("exeRelativePath", out var exe) ? exe.GetString() ?? "" : "";
            g.LaunchArgs = el.TryGetProperty("launchArgs", out var la) ? la.GetString() ?? "" : "";
            g.CoverImagePath = el.TryGetProperty("coverImagePath", out var ci) ? ci.GetString() ?? "" : "";
            if (el.TryGetProperty("tags", out var tg) && tg.ValueKind == JsonValueKind.Array)
            {
                g.Tags = new List<string>();
                foreach (var t in tg.EnumerateArray())
                    g.Tags.Add(t.GetString() ?? "");
            }
            g.Compatibility = el.TryGetProperty("compatibility", out var comp)
                ? (GameCompatibility)(comp.ValueKind == JsonValueKind.Number ? comp.GetInt32() : 0)
                : GameCompatibility.Unknown;
            g.PlayCount = el.TryGetProperty("playCount", out var pc) ? pc.GetInt32() : 0;
            g.TotalPlayTimeSec = el.TryGetProperty("totalPlayTimeSec", out var tpt) ? tpt.GetInt64() : 0;
            g.LastPlayed = el.TryGetProperty("lastPlayed", out var lp) ? lp.GetInt64() : 0;
            g.AddedDate = el.TryGetProperty("addedDate", out var ad) ? ad.GetInt64() : 0;
            return g;
        }

        private static ScanResultItem ParseScanItem(JsonElement el)
        {
            var item = new ScanResultItem();
            if (el.ValueKind != JsonValueKind.Object) return item;
            item.Name = el.TryGetProperty("name", out var n) ? n.GetString() ?? "" : "";
            item.SmbPath = el.TryGetProperty("smbPath", out var sp) ? sp.GetString() ?? "" : "";
            item.ExeRelativePath = el.TryGetProperty("exeRelativePath", out var exe) ? exe.GetString() ?? "" : "";
            item.CoverImagePath = el.TryGetProperty("coverImagePath", out var ci) ? ci.GetString() ?? "" : "";
            item.HasCover = el.TryGetProperty("hasCover", out var hc) && hc.GetBoolean();
            return item;
        }
    }
}
