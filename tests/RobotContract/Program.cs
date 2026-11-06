using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using ModelContextProtocol.Client;
using ModelContextProtocol.Protocol;
using VoiceInput.Robot;

internal static class Program
{
    private static int passed;
    private static void Check(bool condition, string message)
    { if (!condition) throw new InvalidOperationException(message); }

    private static async Task Test(string name, Func<Task> test)
    {
        await test();
        passed++;
        Console.WriteLine("PASS " + name);
    }

    public static async Task Main(string[] args)
    {
        if (args.Contains("--pairing-contract")) { await PairingContract.RunAll(); return; }
        if (args.Contains("--wifi-contract")) { await WifiContract.RunAll(); return; }
        await Test("start, type, send, stop", async () =>
        {
            var input = new FakeInput();
            await using var session = new RobotTextSession(input, () => true);
            Check((await session.ExecuteAsync("start")).Ok, "start");
            Check((await session.ExecuteAsync("type", "检查这个接口")).Ok, "type");
            Check((await session.ExecuteAsync("send", "发送")).Ok, "send");
            Check((await session.ExecuteAsync("stop")).Ok, "stop");
            Check(input.Actions.SequenceEqual(["type:检查这个接口", "send"]), "order");
            Check(!session.Listening, "stopped");
        });
        await Test("embedded send words remain text", async () =>
        {
            var input = new FakeInput();
            await using var session = new RobotTextSession(input, () => true);
            await session.ExecuteAsync("start");
            await session.ExecuteAsync("type", "帮我增加发送按钮");
            await session.ExecuteAsync("type", "先不要发送代码");
            Check(input.Actions.All(item => item.StartsWith("type:")), "must never send");
        });
        await Test("explicit full send phrase is required", async () =>
        {
            var input = new FakeInput();
            await using var session = new RobotTextSession(input, () => true);
            await session.ExecuteAsync("start");
            await session.ExecuteAsync("type", "正文");
            foreach (var value in new string?[] { null, "", "帮我发送", "发送之后退出" })
                Check(!(await session.ExecuteAsync("send", value)).Ok, "must reject");
            Check(input.Actions.Count == 1, "no side effects");
        });
        await Test("repeated send does not submit twice", async () =>
        {
            var input = new FakeInput();
            await using var session = new RobotTextSession(input, () => true);
            await session.ExecuteAsync("start");
            await session.ExecuteAsync("type", "正文");
            await session.ExecuteAsync("send", "发送");
            Check(!(await session.ExecuteAsync("send", "发送")).Ok, "repeated send");
            Check(input.Actions.Count(item => item == "send") == 1, "only one send");
        });
        await Test("send waits behind previous insertion", async () =>
        {
            var input = new FakeInput { Block = true };
            await using var session = new RobotTextSession(input, () => true);
            await session.ExecuteAsync("start");
            var typing = session.ExecuteAsync("type", "第一句");
            await input.Entered.Task.WaitAsync(TimeSpan.FromSeconds(2));
            var sending = session.ExecuteAsync("send", "发送");
            Check(input.Actions.IsEmpty, "send must wait");
            input.Release.TrySetResult();
            await Task.WhenAll(typing, sending);
            Check(input.Actions.SequenceEqual(["type:第一句", "send"]), "serialized");
        });
        await Test("exit cancels in-flight and queued text", async () =>
        {
            var input = new FakeInput { Block = true };
            await using var session = new RobotTextSession(input, () => true);
            await session.ExecuteAsync("start");
            var first = session.ExecuteAsync("type", "旧聊天内容");
            await input.Entered.Task.WaitAsync(TimeSpan.FromSeconds(2));
            var second = session.ExecuteAsync("type", "不应追加");
            await session.ExecuteAsync("stop");
            await Task.WhenAll(first, second);
            Check(input.Actions.IsEmpty, "no late input");
            input.Block = false;
            await session.ExecuteAsync("start");
            Check((await session.ExecuteAsync("type", "新内容")).Ok, "can resume");
            Check(input.Actions.SequenceEqual(["type:新内容"]), "only new text");
        });
        await Test("input error pauses without retry", async () =>
        {
            var input = new FakeInput { Fail = true };
            await using var session = new RobotTextSession(input, () => true);
            var notices = new List<RobotNotice>();
            session.Changed += notice => notices.Add(notice);
            await session.ExecuteAsync("start");
            Check(!(await session.ExecuteAsync("type", "应保留的原文")).Ok, "failed input");
            Check(!session.Listening, "paused");
            Check(!(await session.ExecuteAsync("send", "发送")).Ok, "cannot send after failure");
            Check(notices.Any(n => n.Error && n.Text == "应保留的原文"), "keep original");
        });
        await Test("queue overflow pauses all pending work", async () =>
        {
            var input = new FakeInput { Block = true };
            await using var session = new RobotTextSession(input, () => true);
            await session.ExecuteAsync("start");
            var tasks = new List<Task<RobotResult>> { session.ExecuteAsync("type", "第一句") };
            await input.Entered.Task.WaitAsync(TimeSpan.FromSeconds(2));
            for (var i = 0; i < 8; i++) tasks.Add(session.ExecuteAsync("type", "排队" + i));
            Check(!(await session.ExecuteAsync("type", "溢出")).Ok, "queue full");
            await Task.WhenAll(tasks);
            Check(!session.Listening && input.Actions.IsEmpty, "pause instead of partial submission");
        });
        await Test("off mode and status do not type", async () =>
        {
            var input = new FakeInput();
            await using var session = new RobotTextSession(input, () => true);
            Check(!(await session.ExecuteAsync("type", "未开启")).Ok, "off");
            Check((await session.ExecuteAsync("status")).Ok, "status");
            Check(!(await session.ExecuteAsync("unknown")).Ok, "unknown");
            Check(input.Actions.IsEmpty, "no input");
        });
        await Test("command normalization preserves sentences", async () =>
        {
            Check(RobotTextSession.Command("开启 Codex 语音输入功能。") == "start", "start normalization");
            Check(RobotTextSession.Command(" 发送。 ") == "send", "send normalization");
            Check(RobotTextSession.Command("退出语音输入功能") == "stop", "stop normalization");
            Check(RobotTextSession.Command("发送之后退出") == "type", "sentence");
            var input = new FakeInput();
            await using var session = new RobotTextSession(input, () => true);
            await session.ExecuteAsync("type", "开启 Codex 语音输入功能");
            Check(session.Listening && input.Actions.IsEmpty, "text start fallback");
        });
        await Test("disabled bridge never types", async () =>
        {
            var input = new FakeInput();
            await using var session = new RobotTextSession(input, () => false);
            Check(!(await session.ExecuteAsync("start")).Ok, "disabled start");
            Check(!(await session.ExecuteAsync("type", "正文")).Ok, "disabled type");
            Check(input.Actions.IsEmpty, "disabled");
        });
        await Test("device success waits for completed text insertion", async () =>
        {
            var input = new FakeInput { Block = true };
            await using var session = new RobotTextSession(input, () => true);
            var tools = new RobotTools(session);
            var started = await tools.VoiceInputAsync("start");
            Check(Feedback(started) == "语音输入已开启", "clear start acknowledgement");
            var typing = tools.VoiceInputAsync("type", "等待输入完成");
            await input.Entered.Task.WaitAsync(TimeSpan.FromSeconds(2));
            Check(!typing.IsCompleted, "no ready cue before insertion");
            await tools.VoiceInputAsync("stop");
            var cancelled = await typing;
            Check(cancelled.IsError == true && Feedback(cancelled) != "叮", "cancelled input must not cue success");
        });
        await Test("robot response has clear mode acknowledgements and silent dictation", async () =>
        {
            var input = new FakeInput();
            await using var session = new RobotTextSession(input, () => true);
            var tools = new RobotTools(session);
            await tools.VoiceInputAsync("start");
            var typed = await tools.VoiceInputAsync("type", "增加发送按钮");
            Check(Feedback(typed) == "", "device cue must never be model TTS");
            Check(typed.StructuredContent?.GetProperty("message").GetString() == "", "no long success message");
            Check(!typed.StructuredContent!.Value.TryGetProperty("voiceReply", out _), "no duplicated spoken cue field");
            var sent = await tools.VoiceInputAsync("type", "发送");
            Check(Feedback(sent) == "" && sent.StructuredContent?.GetProperty("replyPolicy").GetString() == "silent",
                "text fallback send remains silent");
            var stopped = await tools.VoiceInputAsync("stop");
            Check(Feedback(stopped) == "已退出语音输入", "clear exit acknowledgement");
            Check(input.Actions.SequenceEqual(["type:增加发送按钮", "send"]), "cue never becomes typed content");
        });
        await Test("failed input has an error instead of a success cue", async () =>
        {
            var input = new FakeInput { Fail = true };
            await using var session = new RobotTextSession(input, () => true);
            var tools = new RobotTools(session);
            await tools.VoiceInputAsync("start");
            var failure = await tools.VoiceInputAsync("type", "失败正文");
            Check(failure.IsError == true && Feedback(failure).Length > 0 && Feedback(failure) != "叮",
                "real failure is not announced as success");
            Check(!session.Listening && input.Actions.IsEmpty, "failure pauses and never sends");
        });

        await Test("official MCP HTTP protocol and authentication", McpRoundTrip);
        passed += await CloudContract.RunAll();
        passed += await UsbFeedbackContract.RunAll();
        passed += await DirectSpeechContract.RunAll();
        passed += await WifiContract.RunAll();
        passed += await PairingContract.RunAll();
        passed += DiscoveryContract.RunAll();
        Console.WriteLine("Robot integration: " + passed + " tests passed; no native input or user credentials used.");
    }

    private static string Feedback(CallToolResult value) =>
        string.Concat(value.Content.OfType<TextContentBlock>().Select(block => block.Text));

    private static int FreePort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }

    private static async Task McpRoundTrip()
    {
        var input = new FakeInput();
        await using var session = new RobotTextSession(input, () => true);
        await using var server = new RobotMcpServer(session, "test-only-secret");
        var port = FreePort();
        await server.StartAsync(port);
        var endpoint = new Uri("http://127.0.0.1:" + port + "/mcp");
        using var http = new HttpClient(new HttpClientHandler { UseProxy = false });
        using (var denied = await http.GetAsync(endpoint))
            Check(denied.StatusCode == HttpStatusCode.Unauthorized, "missing token");
        using (var request = new HttpRequestMessage(HttpMethod.Get, endpoint))
        {
            request.Headers.Add("Authorization", "Bearer test-only-secret");
            request.Headers.Add("Origin", "https://example.com");
            using var denied = await http.SendAsync(request);
            Check(denied.StatusCode == HttpStatusCode.Unauthorized, "browser origin");
        }
        var transport = new HttpClientTransport(new HttpClientTransportOptions
        {
            Endpoint = endpoint,
            AdditionalHeaders = new Dictionary<string, string> { ["Authorization"] = "Bearer test-only-secret" }
        }, http);
        await using var client = await McpClient.CreateAsync(transport);
        var tools = await client.ListToolsAsync();
        Check(tools.Count == 1 && tools[0].Name == "codex_voice_input", "one tool");
        Check(tools[0].JsonSchema.GetRawText().Contains("action"), "action schema");
        async Task Call(string action, string? text = null, bool success = true)
        {
            var args = new Dictionary<string, object?> { ["action"] = action };
            if (text is not null) args["text"] = text;
            var response = await client.CallToolAsync("codex_voice_input", args);
            Check((response.IsError != true) == success, "MCP tool status");
            Check(response.StructuredContent?.GetProperty("ok").GetBoolean() == success, "structured result preserved");
            if (success && action is "type" or "send") Check(Feedback(response) == "", "HTTP silent dictation");
            if (success && action == "start") Check(Feedback(response) == "语音输入已开启", "HTTP start acknowledgement");
            if (success && action == "stop") Check(Feedback(response) == "已退出语音输入", "HTTP stop acknowledgement");
        }
        await Call("status");
        Check(input.Actions.IsEmpty, "status is read-only");
        await Call("start");
        await Call("type", "增加发送按钮");
        Check(input.Actions.SequenceEqual(["type:增加发送按钮"]), "body not send");
        await Call("send", "发送");
        await Call("send", "发送", success: false);
        await Call("stop");
        Check(input.Actions.SequenceEqual(["type:增加发送按钮", "send"]), "MCP complete flow");
        Check(!session.Listening, "MCP stopped");
    }

    private sealed class FakeInput : IRobotInput
    {
        public ConcurrentQueue<string> Actions { get; } = new();
        public bool Block { get; set; }
        public bool Fail { get; set; }
        public TaskCompletionSource Entered { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public TaskCompletionSource Release { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public async Task AppendAsync(string text, CancellationToken token)
        {
            Entered.TrySetResult();
            if (Block) await Release.Task.WaitAsync(token);
            token.ThrowIfCancellationRequested();
            if (Fail) throw new InvalidOperationException("simulated focus loss");
            Actions.Enqueue("type:" + text);
        }
        public Task SendAsync(CancellationToken token)
        {
            token.ThrowIfCancellationRequested();
            if (Fail) throw new InvalidOperationException("simulated send failure");
            Actions.Enqueue("send");
            return Task.CompletedTask;
        }
    }
}
