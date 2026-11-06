using System.Threading.Channels;
using VoiceInput.Core.Text;

namespace VoiceInput.Robot;

public interface IRobotInput
{
    Task AppendAsync(string text, CancellationToken cancellationToken);
    Task SendAsync(CancellationToken cancellationToken);
}

public sealed record RobotNotice(string Message, bool Listening, bool Error = false, string? Text = null, bool Processing = false);
public sealed record RobotResult(bool Ok, string Message, bool Active);

/// <summary>Only sequences text and explicit commands. Reuses the upstream input and text processing.</summary>
public sealed class RobotTextSession(IRobotInput input, Func<bool> enabled, IRobotFeedback? feedback = null) : IAsyncDisposable
{
    private sealed record Work(string Action, string Text, int Epoch, CancellationToken EpochToken,
        CancellationToken RequestToken, TaskCompletionSource<RobotResult> Completion, long Turn = 0);
    private readonly object gate = new();
    private readonly Channel<Work> queue = Channel.CreateBounded<Work>(new BoundedChannelOptions(8)
    { SingleReader = true, FullMode = BoundedChannelFullMode.Wait });
    private readonly List<CancellationTokenSource> previousCancellations = [];
    private CancellationTokenSource cancellation = new();
    private Task? worker;
    private bool listening, unsent, disposed, paused;
    private int epoch, pending;
    private string speechConnection = "";
    private long speechTurn;
    public event Action<string, string>? Audit;

    public event Action<RobotNotice>? Changed;
    public bool Listening { get { lock (gate) return listening; } }
    public bool Paused { get { lock (gate) return paused; } }
    public int InputEpoch => Volatile.Read(ref epoch);
    public int Pending => Volatile.Read(ref pending);

    public static string Command(string text)
    {
        var value = string.Concat(text.ToLowerInvariant().Where(c => !char.IsWhiteSpace(c)))
            .Trim('，', '。', '！', '？', ',', '.', '!', '?', '"', '“', '”');
        return value switch
        {
            "开启语言输入" or "打开语言输入" or "开启语言输入功能" or "打开语言输入功能" or "开启codex语音输入功能" or "开启codex语言输入功能" or "开启codex语音输入" or "开启语音输入" or "开启语音输入功能" or "打开语音输入功能" or "打开语音输入" or "开启语音输入模式" or "打开语音输入模式" or "打开codex语音输入" or "打开codex语音输入功能" => "start",
            "退出语音输入功能" or "退出codex语音输入功能" or "退出语音输入" or "退出模式" or "退出语音输入模式" => "stop",
            "发送" => "send",
            _ => "type"
        };
    }

    public void SetMode(bool active, bool resetFeedback = true)
    {
        CancellationTokenSource? old = null;
        lock (gate)
        {
            if (disposed) return;
            if (active && !enabled()) throw new InvalidOperationException("机器人输入当前不可用，请检查电脑程序。");
            paused = false;
            if (active)
            {
                if (!listening) unsent = false;
                listening = true;
            }
            else
            {
                listening = false;
                unsent = false;
                epoch++;
                old = cancellation;
                previousCancellations.Add(old);
                cancellation = new();
            }
        }
        old?.Cancel();
        if (!active && resetFeedback) feedback?.Reset();
        Publish(active ? "语音输入已开启" : "语音输入已退出");
    }

    public void ModelTransportReset()
    {
        // This connection only carries read-only model guidance. The paired device transport owns actual dictation.
        if (feedback is not IRobotSpeechFeedback) SetMode(false);
    }

    public void Pause(string message = "输入已暂停，语音输入模式仍保留。请检查连接或目标输入框。", bool notifyFeedback = true)
    {
        if (feedback is not IRobotSpeechFeedback) { SetMode(false); return; }
        CancellationTokenSource? old;
        lock (gate)
        {
            if (disposed || !listening || paused) return;
            paused = true;
            epoch++;
            old = cancellation;
            previousCancellations.Add(old);
            cancellation = new();
        }
        old.Cancel();
        if (notifyFeedback) feedback.Reset();
        Audit?.Invoke("mode_paused", "status");
        Publish(message, true);
    }

    private void ResumeDeviceSpeech(string action)
    {
        lock (gate)
        {
            if (disposed || !enabled() || (listening && !paused)) return;
            if (!listening) unsent = false;
            listening = true; paused = false;
        }
        Audit?.Invoke("device_resumed", action);
        Publish("继续语音输入");
    }

    public Task<RobotResult> ExecuteAsync(string action, string? text = null, CancellationToken token = default)
    {
        if (feedback is not IRobotSpeechFeedback) return ExecuteCoreAsync(action, text, token);
        action = action.Trim().ToLowerInvariant();
        if (action == "status") return Result(true, Paused ? "语音输入已暂停，模式仍保留" : Listening ? "语音输入已开启" : "语音输入未开启");
        Audit?.Invoke("model_blocked", action is "start" or "stop" or "type" or "send" ? action : "unknown");
        // Read-only acknowledgements. Never execute a model-requested start, send, type or stop.
        if (action == "start" && Listening) return Result(true, "语音输入已开启");
        if (action == "stop" && !Listening) return Result(true, "已退出语音输入");
        return Result(false, "本工具只查询状态。实际口令由机器人识别原文经配对连接执行，不调用或重试输入、发送、退出操作。");
    }

    public Task<RobotResult> RecognizeAsync(RobotSpeech speech, CancellationToken token = default)
    {
        if (feedback is not IRobotSpeechFeedback || speech.Turn <= 0 || speech.Text.Length is 0 or > 4000)
            return Result(false, "无效的设备识别事件。");
        lock (gate)
        {
            if (speech.Connection != speechConnection) { speechConnection = speech.Connection; speechTurn = 0; }
            if (speech.Turn <= speechTurn) return Result(false, "重复识别事件已忽略。");
            speechTurn = speech.Turn;
        }
        var action = Command(speech.Text);
        Audit?.Invoke("device_recognized", action);
        if (action is "type" or "send") ResumeDeviceSpeech(action);
        return ExecuteCoreAsync(action, speech.Text, token, speech.Turn);
    }

    private Task<RobotResult> ExecuteCoreAsync(string action, string? text, CancellationToken token, long turn = 0)
    {
        action = action.Trim().ToLowerInvariant();
        text = text?.Trim() ?? "";
        token.ThrowIfCancellationRequested();
        if (action == "status") return Task.FromResult(new RobotResult(true, $"等待处理：{Pending}", Listening));
        if (action == "type")
        {
            if (text.Length is 0 or > 4000) return Result(false, "正文需要1到4000个字。");
            var detected = Command(text);
            if (detected != "type") action = detected;
        }
        if (action == "stop") return EndAsync(token, turn);
        if (action == "start")
        {
            if (!enabled()) return Result(false, "机器人输入当前不可用，请检查电脑程序。");
            return BeginAsync(token, turn);
        }
        if (action is not ("type" or "send")) return Result(false, "不支持的操作。");
        if (action == "send" && Command(text) != "send") return Result(false, "发送需要用户单独说“发送”，并将该原话放入 text。");
        lock (gate)
        {
            if (disposed || !enabled()) return Result(false, "机器人输入当前不可用，请检查电脑程序与无线连接。");
            if (!listening) return Result(false, "请先说“开启 Codex 语音输入功能”。");
            if (paused) return Result(false, "输入已暂停，模式仍保留。");
            var completion = new TaskCompletionSource<RobotResult>(TaskCreationOptions.RunContinuationsAsynchronously);
            if (queue.Writer.TryWrite(new Work(action, text, epoch, cancellation.Token, token, completion, turn)))
            {
                Interlocked.Increment(ref pending);
                worker ??= Task.Run(RunAsync);
                return completion.Task;
            }
        }
        Pause();
        Publish("处理队列已满，已暂停；请核对草稿后重新开启。", true);
        return Result(false, "处理队列已满，已暂停；请核对草稿后重新开启。");
    }

    private async Task<RobotResult> BeginAsync(CancellationToken token, long turn)
    {
        int requestedEpoch;
        lock (gate) requestedEpoch = epoch;
        try
        {
            if (feedback is IRobotSpeechFeedback speech) await speech.SetSpeechModeAsync(true, turn, token);
            else if (feedback is not null) await feedback.SetModeAsync(true, token);
            token.ThrowIfCancellationRequested();
            lock (gate)
            {
                if (disposed || requestedEpoch != epoch || !enabled())
                { feedback?.Reset(); return new(false, "开启已取消，请重新发出开启口令。", false); }
                SetMode(true);
            }
            return new(true, "", Listening);
        }
        catch (OperationCanceledException) { Pause(); throw; }
        catch { Pause(); return new(false, "请检查机器人无线连接及固件协议支持，再重新开启。", false); }
    }

    private async Task<RobotResult> EndAsync(CancellationToken token, long turn)
    {
        SetMode(false, resetFeedback: false);
        try
        {
            if (feedback is IRobotSpeechFeedback speech) await speech.SetSpeechModeAsync(false, turn, token);
            else if (feedback is not null) await feedback.SetModeAsync(false, token);
        }
        catch (OperationCanceledException) { feedback?.Reset(); throw; }
        catch { feedback?.Reset(); return new(false, "电脑已退出听写，机器人连接中断，请检查无线连接。", false); }
        return new(true, "", false);
    }

    private Task<RobotResult> Result(bool ok, string message) => Task.FromResult(new RobotResult(ok, message, Listening));

    private bool Current(Work work)
    { lock (gate) return !disposed && work.Epoch == epoch && !work.EpochToken.IsCancellationRequested; }

    private async Task RunAsync()
    {
        await foreach (var work in queue.Reader.ReadAllAsync())
        {
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(work.EpochToken, work.RequestToken);
            var injected = false;
            try
            {
                if (!Current(work)) { work.Completion.TrySetResult(new(false, "本次已取消。", Listening)); continue; }
                linked.Token.ThrowIfCancellationRequested();
                if (feedback is IRobotSpeechFeedback speechFeedback)
                    await speechFeedback.PrepareSpeechAsync(work.Action, work.Turn, linked.Token);
                Audit?.Invoke("device_authorized", work.Action);
                Publish("正在输入…", processing: true);
                if (work.Action == "send")
                {
                    bool shouldSend;
                    lock (gate) shouldSend = unsent;
                    if (!shouldSend)
                    {
                        if (feedback is IRobotSpeechFeedback skip) await skip.CompleteSpeechAsync("skip", work.Turn, linked.Token);
                        work.Completion.TrySetResult(new(false, "没有新的听写内容，不重复发送。", Listening)); continue;
                    }
                    await input.SendAsync(linked.Token);
                    injected = true;
                    if (Current(work)) { lock (gate) unsent = false; }
                    if (feedback is IRobotSpeechFeedback speechFeedbackSend) await speechFeedbackSend.CompleteSpeechAsync("send", work.Turn, linked.Token);
                    else if (feedback is not null) await feedback.CompleteAsync("send", linked.Token);
                    Audit?.Invoke("input_committed", "send");
                    work.Completion.TrySetResult(new(true, "", Listening));
                    Publish("已发送");
                }
                else
                {
                    var text = TextPostProcessor.Process(work.Text);
                    bool appendSpace;
                    lock (gate) appendSpace = unsent;
                    await input.AppendAsync((appendSpace ? " " : "") + text, linked.Token);
                    injected = true;
                    if (Current(work)) { lock (gate) unsent = true; }
                    if (feedback is IRobotSpeechFeedback speechFeedbackType) await speechFeedbackType.CompleteSpeechAsync("type", work.Turn, linked.Token);
                    else if (feedback is not null) await feedback.CompleteAsync("type", linked.Token);
                    Audit?.Invoke("input_committed", "type");
                    work.Completion.TrySetResult(new(true, "", Listening));
                    Publish("已填入", text: text);
                }
            }
            catch (OperationCanceledException)
            {
                work.Completion.TrySetResult(new(false, "本次已取消，没有自动重试。", Listening));
            }
            catch (Exception)
            {
                if (Current(work))
                {
                    Pause();
                    Publish(injected ? "输入动作已执行，但机器人反馈中断；已暂停，请核对屏幕，勿重复操作。"
                        : "输入未完成，已暂停。请点击 Codex 输入框并核对屏幕。", true,
                        work.Action == "type" ? work.Text : null);
                }
                work.Completion.TrySetResult(new(false, injected ? "输入动作已执行，但机器人反馈中断；请核对屏幕，不要自动重试。"
                    : "目标窗口或输入未确认，已暂停；请核对屏幕，不要自动重试。", Listening));
            }
            finally { Interlocked.Decrement(ref pending); }
        }
    }

    private void Publish(string message, bool error = false, string? text = null, bool processing = false)
    {
        try { Changed?.Invoke(new RobotNotice(message, Listening, error, text, processing)); }
        catch (Exception) { /* A closing UI must not interrupt command completion. */ }
    }

    public async ValueTask DisposeAsync()
    {
        SetMode(false);
        lock (gate) { disposed = true; epoch++; cancellation.Cancel(); queue.Writer.TryComplete(); }
        if (worker is not null)
            try { await worker.WaitAsync(TimeSpan.FromSeconds(5)); } catch (TimeoutException) { }
        if (worker is null || worker.IsCompleted)
        {
            cancellation.Dispose();
            foreach (var old in previousCancellations) old.Dispose();
        }
    }
}
