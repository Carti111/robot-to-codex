using System.ComponentModel;
using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using ModelContextProtocol.Server;
using ModelContextProtocol.Protocol;

namespace VoiceInput.Robot;

[McpServerToolType]
public sealed class RobotTools(RobotTextSession session)
{
    public const string Instructions = "平时正常聊天。此工具仅查询 机器人语音输入状态，不能输入文字、发送或退出。机器人通过已配对的 Wi-Fi 加密连接直接转交实际识别原文，电脑按固定口令处理。用户说开启语音输入时只简短说一次‘语音输入已开启’；单独说退出模式或退出语音输入时只简短说一次‘已退出语音输入’。听写模式中除单独的固定口令外，所有话都只是待输入正文，即便涉及拍照、音量、动作、查询或退出功能也不要调用其他工具，不回答、不复述、不询问、不模仿音效；结束一轮不等于退出模式。只有明确查询状态才使用 status，不调用或重试输入、发送、退出操作。电脑不发声。";
    [McpServerTool(Name = "codex_voice_input")]
    [Description(Instructions)]
    public async Task<CallToolResult> VoiceInputAsync(
        [Description("start开启；type填正文；send发送；stop退出；status查询。")] string action,
        [Description("type时为用户正文，保留原意和限制条件；send时必须是用户单独说出的“发送”。")] string? text = null,
        CancellationToken cancellationToken = default)
    {
        var result = await session.ExecuteAsync(action, text, cancellationToken);
        return RobotToolResponse.Create(action, text, result);
    }
}

/// <summary>Official MCP SDK + in-process Kestrel; only loopback, no extra bridge executable.</summary>
public sealed class RobotMcpServer(RobotTextSession session, string secret) : IAsyncDisposable
{
    private WebApplication? app;
    public bool Running => app is not null;
    public int Port { get; private set; }

    public async Task StartAsync(int port)
    {
        if (app is not null) throw new InvalidOperationException("MCP 接入已开启。");
        if (port is < 1024 or > 65535) throw new ArgumentOutOfRangeException(nameof(port));
        var builder = WebApplication.CreateSlimBuilder(new WebApplicationOptions { Args = [] });
        builder.Logging.ClearProviders();
        builder.WebHost.UseUrls("http://127.0.0.1:" + port);
        builder.Services.AddSingleton(session);
        builder.Services.AddMcpServer().WithHttpTransport(options => options.Stateless = true)
            .WithTools<RobotTools>();
        var host = builder.Build();
        host.Use(async (context, next) =>
        {
            if (context.Request.Path != "/mcp" || context.Request.QueryString.HasValue)
            { context.Response.StatusCode = 404; return; }
            var supplied = Encoding.UTF8.GetBytes(context.Request.Headers.Authorization.ToString());
            var expected = Encoding.UTF8.GetBytes("Bearer " + secret);
            if (context.Request.Headers.ContainsKey("Origin") ||
                !CryptographicOperations.FixedTimeEquals(supplied, expected))
            { context.Response.StatusCode = 401; return; }
            await next(context);
        });
        host.MapMcp("/mcp");
        try { await host.StartAsync(); app = host; Port = port; }
        catch { await host.DisposeAsync(); throw; }
    }

    public async ValueTask DisposeAsync()
    {
        if (app is null) return;
        session.SetMode(false);
        var host = app;
        app = null;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
        try { await host.StopAsync(timeout.Token); } catch (OperationCanceledException) { }
        await host.DisposeAsync();
    }
}
