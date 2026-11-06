using System.IO.Pipelines;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using ModelContextProtocol.Server;

namespace VoiceInput.Robot;

public sealed record RobotConnectionState(string Code, string Message)
{
    public bool Ready => Code == "ready";
}

/// <summary>
/// XiaoZhi's WebSocket framing around the official SDK's stream transport.
/// Follows the same message relay as 78/mcp_pipe; never implements MCP handlers itself.
/// </summary>
public sealed class RobotCloudConnection(
    RobotTextSession session,
    Func<bool>? managerRunning = null,
    Func<Uri, CancellationToken, Task<WebSocket>>? connect = null) : IAsyncDisposable
{
    private readonly CancellationTokenSource lifetime = new();
    private Task? running;
    private int disposed;
    private RobotConnectionState state = new("stopped", "未连接");
    public RobotConnectionState State => Volatile.Read(ref state);
    public event Action<RobotConnectionState>? Changed;
    public string? Endpoint { get; private set; }

    public static bool TryEndpoint(string? value, out Uri? uri)
    {
        uri = null;
        if (string.IsNullOrWhiteSpace(value) || value.Length > 8192 ||
            value.Any(char.IsWhiteSpace) || !Uri.TryCreate(value, UriKind.Absolute, out var parsed))
            return false;
        if (parsed.Scheme != "wss" || parsed.Host != "api.xiaozhi.me" ||
            !parsed.IsDefaultPort || parsed.UserInfo.Length != 0 || parsed.Fragment.Length != 0 ||
            parsed.AbsolutePath.TrimEnd('/') != "/mcp") return false;
        var query = parsed.Query.TrimStart('?').Split('&', StringSplitOptions.RemoveEmptyEntries);
        if (!query.Any(part => part.StartsWith("token=", StringComparison.Ordinal) && part.Length > 6))
            return false;
        uri = parsed;
        return true;
    }

    public void Start(string endpoint)
    {
        if (running is not null) throw new InvalidOperationException("连接已经启动。");
        if (!TryEndpoint(endpoint, out _)) throw new ArgumentException("请填写小智控制台的完整 MCP 接入点地址。");
        Endpoint = endpoint;
        running = Task.Run(() => RunAsync(endpoint, lifetime.Token));
    }

    private void Publish(string code, string message)
    {
        var next = new RobotConnectionState(code, message);
        if (next == State) return;
        Volatile.Write(ref state, next);
        try { Changed?.Invoke(next); } catch { /* Closing UI cannot break the connection. */ }
    }

    private async Task RunAsync(string endpoint, CancellationToken token)
    {
        var retry = 0;
        try
        {
            while (!token.IsCancellationRequested)
            {
                if (managerRunning?.Invoke() == true)
                {
                    session.ModelTransportReset();
                    Publish("manager", "请先退出 MCP 管理器，再使用独立连接");
                    await Task.Delay(2000, token);
                    continue;
                }
                try
                {
                    Publish("connecting", retry == 0 ? "正在连接小智…" : "正在重新连接…");
                    using var attempt = CancellationTokenSource.CreateLinkedTokenSource(token);
                    using var socket = await OpenAsync(new Uri(endpoint), attempt.Token);
                    Publish("registering", "已连接，正在准备语音输入…");
                    var relay = RelayAsync(socket, attempt.Token);
                    var guard = WatchManagerAsync(attempt.Token);
                    try
                    {
                        var first = await Task.WhenAny(relay, guard);
                        await first;
                        if (first == guard)
                            Publish("manager", "管理器已打开，独立连接已暂停");
                    }
                    finally
                    {
                        attempt.Cancel();
                        socket.Abort();
                        try { await Task.WhenAll(relay, guard); } catch { }
                    }
                    retry = 0;
                }
                catch (OperationCanceledException) when (token.IsCancellationRequested) { break; }
                catch
                {
                    // Exceptions can contain the credential-bearing URI; never publish or log them.
                    Publish("reconnecting", "连接中断，正在重试；请检查网络或接入点");
                    retry++;
                }
                finally { session.ModelTransportReset(); }
                await Task.Delay(TimeSpan.FromSeconds(Math.Min(30, 3 + retry * 2)), token);
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        finally { session.ModelTransportReset(); Publish("stopped", "连接已关闭"); }
    }

    private async Task<WebSocket> OpenAsync(Uri endpoint, CancellationToken token)
    {
        if (connect is not null) return await connect(endpoint, token);
        var socket = new ClientWebSocket();
        socket.Options.KeepAliveInterval = TimeSpan.FromSeconds(20);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token);
        timeout.CancelAfter(TimeSpan.FromSeconds(20));
        try { await socket.ConnectAsync(endpoint, timeout.Token); return socket; }
        catch { socket.Dispose(); throw; }
    }

    private async Task WatchManagerAsync(CancellationToken token)
    {
        while (true)
        {
            await Task.Delay(1000, token);
            if (managerRunning?.Invoke() == true) return;
        }
    }

    private async Task RelayAsync(WebSocket socket, CancellationToken token)
    {
        var incoming = new Pipe();
        var outgoing = new Pipe();
        await using var input = incoming.Reader.AsStream();
        await using var output = outgoing.Writer.AsStream();
        await using var transport = new StreamServerTransport(input, output,
            "xiaozhi-websocket", NullLoggerFactory.Instance);
        var tools = new RobotTools(session);
        await using var server = McpServer.Create(transport, new McpServerOptions
        {
            ServerInfo = new() { Name = "codex-voice-input-robot", Version = System.Reflection.Assembly.GetEntryAssembly()?.GetName().Version?.ToString(3) ?? "0.2.1" },
            ServerInstructions = RobotTools.Instructions,
            ToolCollection = [McpServerTool.Create(
                typeof(RobotTools).GetMethod(nameof(RobotTools.VoiceInputAsync))!, tools)]
        }, NullLoggerFactory.Instance);
        using var relayLifetime = CancellationTokenSource.CreateLinkedTokenSource(token);
        var ct = relayLifetime.Token;
        var receiving = ReceiveAsync();
        var sending = SendAsync();
        var serving = server.RunAsync(ct);
        try
        {
            var first = await Task.WhenAny(receiving, sending, serving);
            await first;
        }
        finally
        {
            relayLifetime.Cancel();
            socket.Abort();
            await incoming.Writer.CompleteAsync();
            await outgoing.Reader.CompleteAsync();
            try { await Task.WhenAll(receiving, sending, serving); } catch { }
        }

        async Task ReceiveAsync()
        {
            var buffer = new byte[16384];
            using var message = new MemoryStream();
            while (!ct.IsCancellationRequested)
            {
                var frame = await socket.ReceiveAsync(buffer.AsMemory(), ct);
                if (frame.MessageType == WebSocketMessageType.Close) return;
                if (message.Length + frame.Count > 4 * 1024 * 1024)
                    throw new InvalidDataException("MCP message too large.");
                message.Write(buffer, 0, frame.Count);
                if (!frame.EndOfMessage) continue;
                using var json = JsonDocument.Parse(message.GetBuffer().AsMemory(0, (int)message.Length));
                if (json.RootElement.ValueKind != JsonValueKind.Object)
                    throw new InvalidDataException("Expected JSON-RPC object.");
                var normalized = JsonSerializer.SerializeToUtf8Bytes(json.RootElement);
                await incoming.Writer.WriteAsync(normalized, ct);
                await incoming.Writer.WriteAsync(new byte[] { (byte)'\n' }, ct);
                message.SetLength(0);
            }
        }

        async Task SendAsync()
        {
            using var reader = new StreamReader(outgoing.Reader.AsStream(), Encoding.UTF8);
            while (await reader.ReadLineAsync(ct) is { } line)
            {
                using var json = JsonDocument.Parse(line);
                var registered = json.RootElement.TryGetProperty("result", out var result) &&
                    result.ValueKind == JsonValueKind.Object &&
                    result.TryGetProperty("tools", out var listed) && listed.ValueKind == JsonValueKind.Array;
                await socket.SendAsync(Encoding.UTF8.GetBytes(line).AsMemory(),
                    WebSocketMessageType.Text, true, ct);
                if (registered) Publish("ready", "已连接 · 可以开始语音输入");
            }
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        lifetime.Cancel();
        session.ModelTransportReset();
        if (running is not null)
            try { await running.WaitAsync(TimeSpan.FromSeconds(8)); } catch { }
        lifetime.Dispose();
    }
}
