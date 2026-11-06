using System.Collections.Concurrent;
using System.Text.Json;
using VoiceInput.Robot;

internal static class UsbFeedbackContract
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static async Task Wait(Func<bool> condition)
    { using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5)); while (!condition()) await Task.Delay(10, timeout.Token); }
    public static async Task<int> RunAll()
    {
        var actions = new List<string>();
        var feedback = new Feedback(actions);
        await using (var session = new RobotTextSession(new Input(actions), () => true, feedback))
        {
            await session.ExecuteAsync("start");
            await session.ExecuteAsync("type", "增加发送按钮");
            await session.ExecuteAsync("send", "发送");
            Check(actions.SequenceEqual(new[] { "start", "input", "typed", "send", "sent" }), "device completions must follow native input");
            await session.ExecuteAsync("stop");
            Check(actions.Last() == "stop", "explicit exit must not immediately abort its spoken acknowledgement");
        }
        Console.WriteLine("PASS USB feedback follows completed input; send and exit retain distinct semantics");
        actions = [];
        feedback = new Feedback(actions) { FailCompletion = true };
        await using (var session = new RobotTextSession(new Input(actions), () => true, feedback))
        {
            await session.ExecuteAsync("start");
            var result = await session.ExecuteAsync("type", "已经输入，不应重复");
            Check(!result.Ok && !session.Listening && result.Message.Contains("已执行"), "ambiguous hardware feedback pauses after existing insertion");
            Check(!(await session.ExecuteAsync("send", "发送")).Ok && actions.Count(x => x == "input") == 1 && !actions.Contains("send"), "never replay or send after feedback failure");
        }
        Console.WriteLine("PASS feedback failure after input never retries or falsely claims nothing was typed");
        actions = [];
        feedback = new Feedback(actions) { StartGate = new(TaskCreationOptions.RunContinuationsAsynchronously) };
        await using (var session = new RobotTextSession(new Input(actions), () => true, feedback))
        {
            var start = session.ExecuteAsync("start");
            Check(!(await session.ExecuteAsync("type", "未确认开启")).Ok, "no typing before device mode confirmation");
            await session.ExecuteAsync("stop");
            feedback.StartGate.SetResult();
            Check(!(await start).Ok && !session.Listening && !actions.Contains("input"), "late start cannot undo a later stop");
        }
        Console.WriteLine("PASS late USB start acknowledgement cannot reactivate a stopped session");
        var first = new Wire(); var second = new Wire(); var connections = 0;
        await using (var link = new UsbRobotFeedback(() => Interlocked.Increment(ref connections) == 1 ? first : second))
        {
            await Wait(() => link.Connected);
            await link.SetModeAsync(true, default);
            await link.CompleteAsync("type", default);
            await link.CompleteAsync("send", default);
            Check(first.Operations.Count(x => x == "typed") == 1 && first.Operations.Contains("sent"), "bounded USB protocol");
            first.Fail = true;
            try { await link.StatusAsync(); } catch (IOException) { }
            await Wait(() => connections > 1 && link.Connected);
            Check(!second.Operations.Contains("typed") && !second.Operations.Contains("sent"), "no device command replay on reconnect");
        }
        Console.WriteLine("PASS USB framing ignores logs/spoofed replies; disconnect does not replay success cues or sends");
        var oldWire=new Wire();var tried=0;var nativeRejected=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        await using(var native=new UsbRobotFeedback(()=>Interlocked.Increment(ref tried)==1?oldWire:null,requireLocalMode:true)) {
            native.Changed+=state=>{if(state.Code.Contains("robot_native_mode_required"))nativeRejected.TrySetResult();};
            await Wait(()=>native.ConnectionCode.Contains("robot_native_mode_required"));
            Check(!native.Connected&&!oldWire.Operations.Contains("start"),"old firmware must never receive native actions");
        }
        Console.WriteLine("PASS native-only app rejects firmware without the built-in mode capability");
        await using(var generic=new UsbRobotFeedback(()=>new Wire("cvi-robot",true),requireLocalMode:true)) {
            await Wait(()=>generic.Connected);
            await generic.SetSpeechModeAsync(true,1,default);
        }
        Console.WriteLine("PASS generic robot profile accepts native mode without requiring SparkBot identity");
        return 6;
    }

    private sealed class Input(List<string> actions) : IRobotInput
    {
        public Task AppendAsync(string text, CancellationToken token) { actions.Add("input"); return Task.CompletedTask; }
        public Task SendAsync(CancellationToken token) { actions.Add("send"); return Task.CompletedTask; }
    }
    private sealed class Feedback(List<string> actions) : IRobotFeedback
    {
        public bool FailCompletion;
        public TaskCompletionSource? StartGate;
        public async Task SetModeAsync(bool active, CancellationToken token)
        { actions.Add(active ? "start" : "stop"); if (active && StartGate is not null) await StartGate.Task; }
        public Task CompleteAsync(string action, CancellationToken token)
        { if (FailCompletion) throw new IOException("test disconnect"); actions.Add(action == "type" ? "typed" : "sent"); return Task.CompletedTask; }
        public void Reset() { actions.Add("abort"); }
    }
    private sealed class Wire(string board = "esp-sparkbot",bool localMode = false) : IRobotFeedbackWire
    {
        private readonly BlockingCollection<string> replies = new();
        public readonly ConcurrentQueue<string> Operations = new();
        public volatile bool Fail;
        public void WriteLine(string line)
        {
            if (Fail) throw new IOException("test wire lost");
            using var json = JsonDocument.Parse(line[6..]);
            var value = json.RootElement;
            var session = value.GetProperty("session").GetString();
            var id = value.GetProperty("id").GetInt32();
            Operations.Enqueue(value.GetProperty("op").GetString()!);
            replies.Add("ordinary boot log");
            replies.Add("@CVI1 " + JsonSerializer.Serialize(new { v=1, session="wrong-session", id, board="esp-sparkbot", speech=true, mode_lock=true, ok=true }));
            replies.Add("@CVI1 " + JsonSerializer.Serialize(new { v=1, session, id, board="wrong-board", ok=true }));
            replies.Add("@CVI1 " + JsonSerializer.Serialize(new { v=1, session, id, board, speech=true, mode_lock=true, local_mode=localMode, ok=true }));
        }
        public string? ReadLine() { if (Fail) throw new IOException("test wire lost"); return replies.TryTake(out var value, 50) ? value : null; }
        public void Dispose() { Fail = true; }
    }
}
