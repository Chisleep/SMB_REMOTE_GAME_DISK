using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace RemoteGameHub.UI.Services
{
    /// <summary>
    /// IPC 响应数据包，对应后台 IpcResponse
    /// </summary>
    public class IpcResponse
    {
        /// <summary>动作名</summary>
        public string Action { get; set; } = string.Empty;
        /// <summary>会话ID</summary>
        public string SessionId { get; set; } = string.Empty;
        /// <summary>是否成功</summary>
        public bool Success { get; set; }
        /// <summary>错误信息</summary>
        public string Error { get; set; } = string.Empty;
        /// <summary>简单参数（字符串键值对）</summary>
        public Dictionary<string, string> Params { get; set; } = new();
        /// <summary>原始 data 节点（JsonElement，可为 null）</summary>
        public JsonElement? Data { get; set; }
        /// <summary>原始 JSON 文本</summary>
        public string RawJson { get; set; } = string.Empty;
    }

    /// <summary>
    /// 事件参数：服务端推送的事件
    /// </summary>
    public class IpcEventArgs : EventArgs
    {
        public string Action { get; set; } = string.Empty;
        public bool Success { get; set; }
        public string Message { get; set; } = string.Empty;
        public Dictionary<string, string> Params { get; set; } = new();
        public JsonElement? Data { get; set; }
        public string RawJson { get; set; } = string.Empty;
    }

    /// <summary>
    /// 命名管道客户端，连接后台服务 \\.\pipe\RemoteGameHub
    /// 采用 newline 分隔的 JSON 行协议，支持请求/响应与事件推送
    /// </summary>
    public class PipeClientService : IDisposable
    {
        // 管道名称与完整路径
        private const string PipeName = "RemoteGameHub";
        private const string FullPipePath = @"\\.\pipe\RemoteGameHub";

        private NamedPipeClientStream? _client;
        private StreamReader? _reader;
        private StreamWriter? _writer;
        private CancellationTokenSource? _listenCts;
        private bool _disposed;

        // 等待响应的请求任务表：sessionId -> TaskCompletionSource
        private readonly ConcurrentDictionary<string, TaskCompletionSource<IpcResponse>> _pending
            = new();

        // 自增 sessionId
        private int _sessionSeq;

        /// <summary>是否已连接</summary>
        public bool IsConnected => _client?.IsConnected ?? false;

        /// <summary>连接状态变化事件</summary>
        public event EventHandler<bool>? ConnectionChanged;

        /// <summary>接收到服务端推送事件（非响应类消息）</summary>
        public event EventHandler<IpcEventArgs>? OnEventReceived;

        /// <summary>
        /// 异步连接后台服务管道
        /// </summary>
        /// <param name="timeoutMs">连接超时（毫秒）</param>
        public async Task ConnectAsync(int timeoutMs = 5000)
        {
            if (IsConnected) return;
            try
            {
                _client = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut,
                    PipeOptions.Asynchronous);
                await _client.ConnectAsync(timeoutMs);

                _reader = new StreamReader(_client, new UTF8Encoding(false));
                _writer = new StreamWriter(_client, new UTF8Encoding(false))
                {
                    AutoFlush = true,
                    NewLine = "\n"
                };

                _listenCts = new CancellationTokenSource();
                _ = Task.Run(() => ListenLoop(_listenCts.Token));

                ConnectionChanged?.Invoke(this, true);
            }
            catch
            {
                ConnectionChanged?.Invoke(this, false);
                throw;
            }
        }

        /// <summary>
        /// 断开连接
        /// </summary>
        public void Disconnect()
        {
            try { _listenCts?.Cancel(); } catch { }
            try { _writer?.Dispose(); } catch { }
            try { _reader?.Dispose(); } catch { }
            try { _client?.Dispose(); } catch { }
            _writer = null;
            _reader = null;
            _client = null;

            // 通知所有挂起请求失败
            foreach (var kv in _pending)
            {
                kv.Value.TrySetException(new IOException("管道已断开"));
            }
            _pending.Clear();

            ConnectionChanged?.Invoke(this, false);
        }

        /// <summary>
        /// 发送请求并等待响应
        /// </summary>
        /// <param name="action">动作名（如 get_game_list）</param>
        /// <param name="params">简单参数</param>
        /// <param name="jsonData">复杂数据的 JSON 文本（写入 data 字段），可为 null</param>
        /// <param name="timeoutMs">等待响应超时</param>
        public async Task<IpcResponse> SendRequestAsync(
            string action,
            Dictionary<string, string>? @params = null,
            string? jsonData = null,
            int timeoutMs = 15000)
        {
            if (!IsConnected || _writer == null)
                throw new IOException("未连接到后台服务");

            // 生成唯一 sessionId
            int seq = Interlocked.Increment(ref _sessionSeq);
            string sessionId = $"{DateTimeOffset.UtcNow.ToUnixTimeSeconds():x}{seq:x}";

            var tcs = new TaskCompletionSource<IpcResponse>(TaskCreationOptions.RunContinuationsAsynchronously);
            _pending[sessionId] = tcs;

            // 构造请求 JSON
            string json = BuildRequestJson(action, sessionId, @params, jsonData);
            await _writer.WriteLineAsync(json);

            // 等待响应或超时
            using var cts = new CancellationTokenSource(timeoutMs);
            using cts.Token.Register(() => tcs.TrySetException(new TimeoutException($"请求 {action} 超时")));
            try
            {
                return await tcs.Task;
            }
            finally
            {
                _pending.TryRemove(sessionId, out _);
            }
        }

        /// <summary>
        /// 构造请求 JSON 文本
        /// </summary>
        private static string BuildRequestJson(
            string action, string sessionId,
            Dictionary<string, string>? @params, string? jsonData)
        {
            using var sw = new StringWriter();
            using var jw = new JsonTextWriter(sw);
            jw.WriteStartObject();
            jw.WritePropertyName("action"); jw.WriteValue(action);
            jw.WritePropertyName("sessionId"); jw.WriteValue(sessionId);

            jw.WritePropertyName("params");
            jw.WriteStartObject();
            if (@params != null)
            {
                foreach (var kv in @params)
                {
                    jw.WritePropertyName(kv.Key); jw.WriteValue(kv.Value);
                }
            }
            jw.WriteEndObject();

            jw.WritePropertyName("data");
            if (string.IsNullOrEmpty(jsonData) || jsonData == "null")
                jw.WriteNull();
            else
            {
                // 直接写入原始 JSON 片段
                jw.WriteRawValue(jsonData);
            }

            jw.WriteEndObject();
            return sw.ToString();
        }

        /// <summary>
        /// 监听循环：逐行读取服务端消息并分发
        /// </summary>
        private async Task ListenLoop(CancellationToken token)
        {
            var reader = _reader;
            if (reader == null) return;

            while (!token.IsCancellationRequested)
            {
                string? line;
                try
                {
                    line = await reader.ReadLineAsync(token);
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch
                {
                    // 读取异常视为断开
                    break;
                }

                if (line == null) break; // 对端关闭
                if (string.IsNullOrWhiteSpace(line)) continue;

                try
                {
                    HandleIncomingMessage(line);
                }
                catch
                {
                    // 单条消息解析失败不影响后续
                }
            }

            ConnectionChanged?.Invoke(this, false);
        }

        /// <summary>
        /// 处理一条入站消息：响应则匹配挂起请求，否则作为事件分发
        /// </summary>
        private void HandleIncomingMessage(string json)
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            string action = root.TryGetProperty("action", out var actEl) ? actEl.GetString() ?? "" : "";
            string sessionId = root.TryGetProperty("sessionId", out var sidEl) ? sidEl.GetString() ?? "" : "";
            bool success = root.TryGetProperty("success", out var sucEl) && sucEl.GetBoolean();
            string error = root.TryGetProperty("error", out var errEl) ? errEl.GetString() ?? "" : "";

            var resp = new IpcResponse
            {
                Action = action,
                SessionId = sessionId,
                Success = success,
                Error = error,
                RawJson = json
            };

            // 解析 params
            if (root.TryGetProperty("params", out var pEl) && pEl.ValueKind == JsonValueKind.Object)
            {
                foreach (var prop in pEl.EnumerateObject())
                {
                    string val = prop.Value.ValueKind == JsonValueKind.String
                        ? prop.Value.GetString() ?? ""
                        : prop.Value.GetRawText();
                    resp.Params[prop.Name] = val;
                }
            }

            // 解析 data
            if (root.TryGetProperty("data", out var dEl) && dEl.ValueKind != JsonValueKind.Null)
            {
                resp.Data = dEl.Clone();
            }

            // 事件类动作优先作为事件分发（即使 sessionId 命中挂起请求）
            // 这些动作是服务端主动推送的进度/状态通知，不应作为请求响应
            if (IsEventAction(action))
            {
                var evtArgs = new IpcEventArgs
                {
                    Action = action,
                    Success = success,
                    Message = resp.Params.TryGetValue("message", out var msg) ? msg : error,
                    Params = resp.Params,
                    Data = resp.Data,
                    RawJson = json
                };
                OnEventReceived?.Invoke(this, evtArgs);
                return;
            }

            // 响应匹配：sessionId 命中挂起请求则完成它
            if (!string.IsNullOrEmpty(sessionId) && _pending.TryRemove(sessionId, out var tcs))
            {
                tcs.TrySetResult(resp);
                return;
            }

            // 其余未匹配的消息也作为事件分发
            var args = new IpcEventArgs
            {
                Action = action,
                Success = success,
                Message = resp.Params.TryGetValue("message", out var msg2) ? msg2 : error,
                Params = resp.Params,
                Data = resp.Data,
                RawJson = json
            };
            OnEventReceived?.Invoke(this, args);
        }

        /// <summary>
        /// 判断是否为服务端主动推送的事件类动作
        /// </summary>
        private static bool IsEventAction(string action)
        {
            return action switch
            {
                "scan_progress" or "scan_complete" or
                "launch_progress" or "launch_complete" or "launch_failed" or
                "game_exited" => true,
                _ => false
            };
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            Disconnect();
        }
    }

    /// <summary>
    /// 简易 JsonTextWriter 包装，便于构造请求 JSON
    /// </summary>
    internal sealed class JsonTextWriter : IDisposable
    {
        private readonly TextWriter _writer;
        private int _depth;

        public JsonTextWriter(TextWriter writer) { _writer = writer; }

        public void WriteStartObject() { _writer.Write('{'); _depth++; }
        public void WriteEndObject() { _writer.Write('}'); _depth--; }
        public void WritePropertyName(string name) { _writer.Write('"'); WriteEscaped(name); _writer.Write("\":"); }
        public void WriteValue(string? v)
        {
            if (v == null) { _writer.Write("null"); return; }
            _writer.Write('"'); WriteEscaped(v); _writer.Write('"');
        }
        public void WriteValue(bool v) => _writer.Write(v ? "true" : "false");
        public void WriteNull() => _writer.Write("null");
        public void WriteRawValue(string raw) => _writer.Write(raw);

        private void WriteEscaped(string s)
        {
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': _writer.Write("\\\""); break;
                    case '\\': _writer.Write("\\\\"); break;
                    case '\n': _writer.Write("\\n"); break;
                    case '\r': _writer.Write("\\r"); break;
                    case '\t': _writer.Write("\\t"); break;
                    default:
                        if (c < 0x20) _writer.Write($"\\u{(int)c:x4}");
                        else _writer.Write(c);
                        break;
                }
            }
        }

        public void Dispose() => _writer.Flush();
    }
}
