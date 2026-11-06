using System.Diagnostics;
using System.IO.Ports;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;
using Microsoft.Win32;

namespace VoiceInput.Robot;

public interface IRobotFeedbackWire : IDisposable
{
    void WriteLine(string value);
    string? ReadLine();
}

public sealed record UsbFeedbackState(bool Connected, string Code);

/// <summary>Bounded CVI1 protocol engine shared by paired Wi-Fi and optional maintenance serial wires. Ordinary device logs are discarded.</summary>
public sealed class UsbRobotFeedback : IRobotSpeechFeedback, IAsyncDisposable
{
    private sealed record Request(string Operation, CancellationToken Token, TaskCompletionSource Completion, long Turn = 0);
    private readonly Func<IRobotFeedbackWire?> connect;
    private readonly bool requireLocalMode;
    private readonly Channel<Request> requests = Channel.CreateBounded<Request>(8);
    private readonly CancellationTokenSource lifetime = new();
    private readonly Task worker;
    private IRobotFeedbackWire? wire;
    private volatile bool connected;
    private string session = "";
    private int sequence;
    private int disposed;
    private int resetPending;
    private string lastCode = "initial";
    public event Action<UsbFeedbackState>? Changed;
    public bool Connected => connected;
    public string ConnectionCode => lastCode;
    public string Connection => session;
    public event Action<RobotSpeech>? Speech;

    public UsbRobotFeedback(Func<IRobotFeedbackWire?>? connector = null, bool requireLocalMode = false)
    {
        connect = connector ?? ConnectEspUsb;
        this.requireLocalMode=requireLocalMode;
        worker = Task.Factory.StartNew(Run, CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default);
    }

    public Task SetModeAsync(bool active, CancellationToken cancellationToken) =>
        !active && !connected ? Task.CompletedTask : Submit(active ? "start" : "stop", cancellationToken);
    public Task CompleteAsync(string action, CancellationToken cancellationToken) =>
        Submit(action == "type" ? "typed" : "sent", cancellationToken);
    public Task TestSoundAsync(CancellationToken cancellationToken = default) => Submit("test_sound", cancellationToken);
    public Task StatusAsync(CancellationToken cancellationToken = default) => Submit("status", cancellationToken);
    public void Reset() { if (connected && Interlocked.Exchange(ref resetPending, 1) == 0) _ = QuietReset(); }
    private async Task QuietReset() { try { await Submit("abort", lifetime.Token); } catch { } finally { Interlocked.Exchange(ref resetPending, 0); } }

    public Task SetSpeechModeAsync(bool active, long turn, CancellationToken token) => Submit(active ? "start" : "stop", token, turn);
    public Task PrepareSpeechAsync(string action, long turn, CancellationToken token) => Submit(action == "send" ? "authorize_send" : "authorize_type", token, turn);
    public Task CompleteSpeechAsync(string action, long turn, CancellationToken token) => Submit(action == "send" ? "sent" : action == "skip" ? "skip" : "typed", token, turn);

    private Task Submit(string operation, CancellationToken token, long turn = 0)
    {
        token.ThrowIfCancellationRequested();
        if (!connected || lifetime.IsCancellationRequested)
            throw new IOException("机器人连接未就绪。");
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!requests.Writer.TryWrite(new(operation, token, completion, turn)))
            throw new IOException("机器人反馈队列已满。");
        return completion.Task.WaitAsync(token);
    }

    private void State(bool value, string code)
    {
        if (connected == value && lastCode == code) return;
        connected = value;
        lastCode = code;
        try { Changed?.Invoke(new(value, code)); } catch { }
    }

    private void Run()
    {
        while (!lifetime.IsCancellationRequested)
        {
            Request? current = null;
            var failure = "no_device";
            try
            {
                wire = connect();
                if (wire is null) { lifetime.Token.WaitHandle.WaitOne(1000); continue; }
                session = Guid.NewGuid().ToString("N");
                sequence = 0;
                Exchange("hello");
                State(true, "connected");
                var heartbeat = Stopwatch.StartNew();
                while (!lifetime.IsCancellationRequested)
                {
                    if (requests.Reader.TryRead(out current))
                    {
                        if (current.Token.IsCancellationRequested) current.Completion.TrySetCanceled(current.Token);
                        else { Exchange(current.Operation, current.Turn); current.Completion.TrySetResult(); }
                        current = null;
                    }
                    else HandleEvent(wire.ReadLine());
                    if (lifetime.IsCancellationRequested) break;
                    if (heartbeat.ElapsedMilliseconds >= 1000) { Exchange("ping"); heartbeat.Restart(); }
                }
            }
            catch (Exception exception)
            {
                if (!lifetime.IsCancellationRequested && Environment.GetEnvironmentVariable("VOICEINPUT_USB_DIAGNOSTICS") == "1") Console.Error.WriteLine("USB IO: " + exception.Message + " at " + exception.TargetSite?.Name);
                failure = exception is IOException && (exception.Message is "robot_reply_timeout" or "robot_rejected" or "robot_native_mode_required" or "robot_direct_speech_required" or "robot_mode_lock_required")
                    ? exception.Message : exception.GetType().Name;
                current?.Completion.TrySetException(new IOException("机器人反馈未确认，请核对电脑内容，勿自动重试。"));
            }
            finally
            {
                State(false, lifetime.IsCancellationRequested ? "stopped" : "disconnected_" + failure);
                try { wire?.Dispose(); } catch { }
                wire = null;
                while (requests.Reader.TryRead(out var pending))
                    pending.Completion.TrySetException(new IOException("机器人连接已断开，本次操作不会重放。"));
            }
            lifetime.Token.WaitHandle.WaitOne(1000);
        }
    }

    private void Exchange(string operation, long turn = 0)
    {
        lifetime.Token.ThrowIfCancellationRequested();
        var id = ++sequence;
        wire!.WriteLine("@CVI1 " + JsonSerializer.Serialize(new { v = 1, session, id, op = operation, turn }));
        var deadline = Stopwatch.StartNew();
        while (!lifetime.IsCancellationRequested && deadline.ElapsedMilliseconds < 3000)
        {
            var line = wire.ReadLine();
            if (line is null || !line.StartsWith("@CVI1 ", StringComparison.Ordinal) || line.Length > 20000) continue;
            JsonDocument parsed;
            try { parsed = JsonDocument.Parse(line[6..]); } catch (JsonException) { continue; }
            using (parsed)
            {
                var value = parsed.RootElement;
                if (!Envelope(value)) continue;
                if (!value.TryGetProperty("id", out var responseId) || !responseId.TryGetInt32(out var number) || number != id)
                { HandleEvent(line); continue; }
                if (!value.TryGetProperty("ok", out var ok) || !ok.GetBoolean()) throw new IOException("robot_rejected");
                if (operation == "hello" && (!value.TryGetProperty("speech", out var speech) || speech.ValueKind != JsonValueKind.True))
                    throw new IOException("robot_direct_speech_required");
                if (operation == "hello" && (!value.TryGetProperty("mode_lock", out var modeLock) || modeLock.ValueKind != JsonValueKind.True))
                    throw new IOException("robot_mode_lock_required");
                if(operation=="hello" && requireLocalMode && (!value.TryGetProperty("local_mode",out var local) || local.ValueKind!=JsonValueKind.True))
                    throw new IOException("robot_native_mode_required");
                return;
            }
        }
        lifetime.Token.ThrowIfCancellationRequested();
        throw new IOException("robot_reply_timeout");
    }

    private bool Envelope(JsonElement value) => value.ValueKind == JsonValueKind.Object &&
        value.TryGetProperty("v", out var v) && v.TryGetInt32(out var version) && version == 1 &&
        value.TryGetProperty("session", out var s) && s.GetString() == session &&
        value.TryGetProperty("board", out var b) && b.GetString() is "esp-sparkbot" or "cvi-robot";

    private void HandleEvent(string? line)
    {
        if (line is null || !line.StartsWith("@CVI1 ", StringComparison.Ordinal) || line.Length > 20000) return;
        try
        {
            using var json = JsonDocument.Parse(line[6..]);
            var value = json.RootElement;
            if (!Envelope(value) || !value.TryGetProperty("event", out var e)) return;
            if (e.GetString() == "speech" && value.TryGetProperty("turn", out var t) && t.TryGetInt64(out var turn) && turn > 0 &&
                value.TryGetProperty("text", out var text) && text.ValueKind == JsonValueKind.String && text.GetString() is { Length: > 0 and <= 4000 } raw)
                Speech?.Invoke(new RobotSpeech(session, turn, raw));
            if (e.GetString() is "timeout" or "paused") State(true, "paused");
            if (e.GetString() == "ready") State(true, "ready");
            if (e.GetString() == "tool_blocked") State(true, "tool_blocked");
            if (e.GetString() == "chat") State(true, "chat");
            if (e.GetString() is "heard_start" or "heard_body" or "heard_send" or "heard_stop") State(true,e.GetString()!);
        }
        catch (JsonException) { }
    }

    private static IRobotFeedbackWire? ConnectEspUsb()
    {
        // Match Espressif native USB only; never probe unrelated serial peripherals.
        using var usb = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum\USB");
        if (usb is null) return null;
        var ports = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var deviceName in usb.GetSubKeyNames().Where(n => n.StartsWith("VID_303A&PID_1001", StringComparison.OrdinalIgnoreCase)))
        {
            using var device = usb.OpenSubKey(deviceName);
            if (device is null) continue;
            foreach (var instance in device.GetSubKeyNames())
            {
                using var parameters = device.OpenSubKey(instance + @"\Device Parameters");
                if (parameters?.GetValue("PortName") is string name && SerialPort.GetPortNames().Contains(name, StringComparer.OrdinalIgnoreCase))
                    ports.Add(name);
            }
        }
        // An ambiguous pair of boards requires explicit selection rather than operating the wrong one.
        return ports.Count == 1 ? new SerialWire(ports.Single()) : null;
    }

    private sealed class SerialWire : IRobotFeedbackWire
    {
        private readonly SerialPort port;
        private readonly StringBuilder line = new();
        private bool overflow;
        public SerialWire(string name)
        {
            port = new SerialPort(name, 115200) { DtrEnable = false, RtsEnable = false,
                ReadTimeout = 100, WriteTimeout = 1500, Encoding = Encoding.UTF8, NewLine = "\n" };
            try { port.Open(); } catch { port.Dispose(); throw; }
        }
        public void WriteLine(string value) { var bytes = Encoding.ASCII.GetBytes("\n" + value + "\n"); for (var offset = 0; offset < bytes.Length; offset += 32) port.Write(bytes, offset, Math.Min(32, bytes.Length - offset)); }
        public string? ReadLine()
        {
            try
            {
                while (true)
                {
                    var c = port.ReadChar();
                    if (c == '\n')
                    {
                        var result = overflow ? null : line.ToString().TrimEnd('\r');
                        line.Clear(); overflow = false; return result;
                    }
                    if (line.Length < 20000) line.Append((char)c); else overflow = true;
                }
            }
            catch (TimeoutException) { return null; }
        }
        public void Dispose() => port.Dispose();
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        try { if (connected) await Submit("abort", CancellationToken.None).WaitAsync(TimeSpan.FromSeconds(3)); } catch { }
        lifetime.Cancel();
        try { await worker.WaitAsync(TimeSpan.FromSeconds(5)); }
        catch (TimeoutException)
        {
            try { wire?.Dispose(); } catch { }
            await worker.WaitAsync(TimeSpan.FromSeconds(2));
        }
        lifetime.Dispose();
    }
}
