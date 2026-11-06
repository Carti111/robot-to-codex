using System.Collections.Concurrent;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Logging;
using VoiceInput.App.Services;
using VoiceInput.Robot;

internal static class CloudContract
{
    private static void Check(bool value, string message)
    { if (!value) throw new InvalidOperationException(message); }

    public static async Task<int> RunAll()
    {
        await TestEndpointAndStorage();
        Console.WriteLine("PASS endpoint validation and user-bound encrypted storage");
        await TestWebSocket();
        Console.WriteLine("PASS direct WebSocket MCP, fragmentation, schema, ordered input and reconnect");
        await TestManagerGuard();
        Console.WriteLine("PASS manager ownership blocks duplicate cloud connection");
        return 3;
    }

    private static Task TestEndpointAndStorage()
    {
        // Old microphone/overlay/hotkey preferences must not recreate removed input paths.
        var migrated=JsonSerializer.Deserialize<AppConfig>(
            "{\"darkMode\":true,\"useComputerMicrophone\":true,\"hotkey\":\"Ctrl+Alt+Space\",\"showAfterInput\":true}",
            new JsonSerializerOptions {PropertyNamingPolicy=JsonNamingPolicy.CamelCase})!;
        Check(migrated.DarkMode,"appearance preserved");
        var saved=JsonSerializer.Serialize(migrated);
        Check(!saved.Contains("Microphone")&&!saved.Contains("Hotkey")&&!saved.Contains("ShowAfterInput"),"removed preferences never saved");
        const string valid = "wss://api.xiaozhi.me/mcp/?token=test-only-not-real";
        Check(RobotCloudConnection.TryEndpoint(valid, out _), "valid endpoint");
        foreach (var value in new[] {
            "ws://api.xiaozhi.me/mcp/?token=x", "wss://example.com/mcp/?token=x",
            "wss://api.xiaozhi.me/mcp/", "wss://api.xiaozhi.me/mcp/?token=",
            "wss://api.xiaozhi.me/else/?token=x", "wss://user@api.xiaozhi.me/mcp/?token=x",
            "wss://api.xiaozhi.me/mcp/?token=x#fragment", " wss://api.xiaozhi.me/mcp/?token=x"
        }) Check(!RobotCloudConnection.TryEndpoint(value, out _), "reject invalid endpoint");
        var directory = Path.Combine(Path.GetTempPath(), "robot-secret-contract-" + Guid.NewGuid().ToString("N"));
        var path = Path.Combine(directory, "endpoint.bin");
        try
        {
            RobotEndpointStore.Save(valid, path);
            Check(RobotEndpointStore.Load(path) == valid, "DPAPI roundtrip");
            Check(!Encoding.UTF8.GetString(File.ReadAllBytes(path)).Contains("test-only-not-real"), "no plaintext");
            File.WriteAllText(path, "corrupt");
            Check(RobotEndpointStore.Load(path) is null, "corrupt data is not used");
            RobotEndpointStore.Save(null, path);
            Check(!File.Exists(path), "remove endpoint");
        }
        finally { if (Directory.Exists(directory)) Directory.Delete(directory, true); }
        return Task.CompletedTask;
    }

    private static async Task TestWebSocket()
    {
        var builder = WebApplication.CreateSlimBuilder();
        builder.Logging.ClearProviders();
        builder.WebHost.UseUrls("http://127.0.0.1:0");
        await using var gateway = builder.Build();
        gateway.UseWebSockets();
        var connected = new ConcurrentQueue<WebSocket>();
        var signal = new SemaphoreSlim(0);
        gateway.Map("/mcp", async context =>
        {
            var socket = await context.WebSockets.AcceptWebSocketAsync();
            connected.Enqueue(socket);
            signal.Release();
            try { await Task.Delay(Timeout.Infinite, context.RequestAborted); }
            catch (OperationCanceledException) { }
            finally { socket.Dispose(); }
        });
        await gateway.StartAsync();
        var uri = new Uri(gateway.Urls.Single().Replace("http://", "ws://") + "/mcp");
        var input = new Input();
        await using var session = new RobotTextSession(input, () => true);
        var manager = false;
        await using var connection = new RobotCloudConnection(session, () => manager, async (_, ct) =>
        {
            var ws = new ClientWebSocket();
            ws.Options.Proxy = null;
            await ws.ConnectAsync(uri, ct);
            return ws;
        });
        connection.Start("wss://api.xiaozhi.me/mcp/?token=test-only-not-real");
        await signal.WaitAsync(TimeSpan.FromSeconds(5));
        Check(connected.TryDequeue(out var socket), "connected socket");
        await Initialize(socket!);
        var tools = await Request(socket!, 2, "tools/list", new { }, fragmented: true);
        var list = tools.GetProperty("result").GetProperty("tools");
        Check(list.GetArrayLength() == 1, "one tool");
        var tool = list[0];
        Check(tool.GetProperty("name").GetString() == "codex_voice_input", "stable tool name");
        Check(tool.GetProperty("inputSchema").GetProperty("properties").TryGetProperty("action", out _), "schema action");
        Check(tool.GetProperty("description").GetString()!.Contains("平时正常聊天"), "mode instructions");
        await WaitFor(() => connection.State.Ready);
        Check((await Call(socket!, 3, "status")).GetProperty("active").GetBoolean() == false, "not auto listening");
        Check(!(await Call(socket!, 4, "type", "普通聊天")).GetProperty("ok").GetBoolean(), "inactive text rejected");
        Check(input.Actions.IsEmpty, "status and chat do not type");
        await Call(socket!, 5, "start");
        await Call(socket!, 6, "type", "给页面增加发送按钮");
        await Call(socket!, 7, "send", "发送");
        await Call(socket!, 8, "send", "发送");
        await Call(socket!, 9, "stop");
        Check(input.Actions.SequenceEqual(new[] { "type:给页面增加发送按钮", "send" }), "exactly one send");
        Check(!session.Listening, "stop exits");
        await Call(socket!, 10, "start");
        await Call(socket!, 11, "type", "不要重复输入");
        socket!.Abort();
        await WaitFor(() => !session.Listening);
        await signal.WaitAsync(TimeSpan.FromSeconds(10));
        Check(connected.TryDequeue(out var second), "reconnected socket");
        await Initialize(second!);
        await Request(second!, 2, "tools/list", new { });
        Check(!(await Call(second!, 3, "status")).GetProperty("active").GetBoolean(), "reconnect stays stopped");
        Check(input.Actions.Count == 3, "no input replay");
        manager = true;
        await WaitFor(() => connection.State.Code == "manager");
        Check(!session.Listening, "manager handover stops listening");
        await connection.DisposeAsync();

        await gateway.StopAsync();
    }

    private static async Task TestManagerGuard()
    {
        var calls = 0;
        await using var session = new RobotTextSession(new Input(), () => true);
        await using var connection = new RobotCloudConnection(session, () => true, (_, _) =>
        {
            Interlocked.Increment(ref calls);
            throw new InvalidOperationException("must never connect");
        });
        connection.Start("wss://api.xiaozhi.me/mcp/?token=test-only-not-real");
        await WaitFor(() => connection.State.Code == "manager");
        Check(calls == 0, "blocked before network");
        Check(!session.Listening, "no listening");
    }

    private static async Task Initialize(WebSocket socket)
    {
        var response = await Request(socket, 1, "initialize", new {
            protocolVersion = "2024-11-05", capabilities = new { },
            clientInfo = new { name = "fake-xiaozhi", version = "1.0" }
        });
        Check(response.GetProperty("result").GetProperty("instructions").GetString()!.Contains("退出语音输入"), "instructions");
        await Send(socket, new { jsonrpc = "2.0", method = "notifications/initialized" });
    }

    private static async Task<JsonElement> Call(WebSocket socket, int id, string action, string? text = null)
    {
        var response = await Request(socket, id, "tools/call",
            new { name = "codex_voice_input", arguments = new { action, text } });
        var result = response.GetProperty("result");
        var value = result.GetProperty("structuredContent").Clone();
        var ok = value.GetProperty("ok").GetBoolean();
        var isError = result.TryGetProperty("isError", out var error) && error.GetBoolean();
        Check(isError == !ok, "MCP error flag agrees with structured state");
        var reply = result.GetProperty("content")[0].GetProperty("text").GetString();
        if (ok && action is "type" or "send") Check(reply == "", "WebSocket silent dictation");
        if (ok && action == "start") Check(reply == "语音输入已开启", "WebSocket start acknowledgement");
        if (ok && action == "stop") Check(reply == "已退出语音输入", "WebSocket stop acknowledgement");
        return value;
    }

    private static async Task<JsonElement> Request(WebSocket socket, int id, string method,
        object parameters, bool fragmented = false)
    {
        await Send(socket, new { jsonrpc = "2.0", id, method, @params = parameters }, fragmented);
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(8));
        while (true)
        {
            var bytes = new byte[16384];
            using var message = new MemoryStream();
            WebSocketReceiveResult received;
            do
            {
                received = await socket.ReceiveAsync(new ArraySegment<byte>(bytes), timeout.Token);
                if (received.MessageType == WebSocketMessageType.Close) throw new InvalidOperationException("closed");
                message.Write(bytes, 0, received.Count);
            } while (!received.EndOfMessage);
            using var json = JsonDocument.Parse(message.ToArray());
            if (json.RootElement.TryGetProperty("id", out var responseId) && responseId.GetInt32() == id)
            {
                Check(!json.RootElement.TryGetProperty("error", out _), "JSON-RPC success: " + method);
                return json.RootElement.Clone();
            }
        }
    }

    private static async Task Send(WebSocket socket, object value, bool fragmented = false)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(value, new JsonSerializerOptions { WriteIndented = true });
        if (fragmented)
        {
            var half = bytes.Length / 2;
            await socket.SendAsync(bytes.AsMemory(0, half), WebSocketMessageType.Text, false, CancellationToken.None);
            await socket.SendAsync(bytes.AsMemory(half), WebSocketMessageType.Text, true, CancellationToken.None);
        }
        else await socket.SendAsync(bytes.AsMemory(), WebSocketMessageType.Text, true, CancellationToken.None);
    }

    private static async Task WaitFor(Func<bool> predicate)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        while (!predicate()) await Task.Delay(30, timeout.Token);
    }

    private sealed class Input : IRobotInput
    {
        public ConcurrentQueue<string> Actions { get; } = new();
        public Task AppendAsync(string text, CancellationToken ct) { ct.ThrowIfCancellationRequested(); Actions.Enqueue("type:" + text); return Task.CompletedTask; }
        public Task SendAsync(CancellationToken ct) { ct.ThrowIfCancellationRequested(); Actions.Enqueue("send"); return Task.CompletedTask; }
    }
}
