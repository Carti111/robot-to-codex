using System.Collections.Concurrent;
using System.Text.Json;
using VoiceInput.Robot;

internal static class DirectSpeechContract
{
    private static void Check(bool value,string name) { if (!value) throw new Exception(name); }
    private static async Task Wait(Func<bool> condition)
    { using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(5)); while(!condition()) await Task.Delay(10,timeout.Token); }
    private static RobotSpeech Speech(long turn,string text)=>new("device-connection",turn,text);
    public static async Task<int> RunAll()
    {
        var actions=new List<string>(); var feedback=new Feedback(actions);
        await using(var session=new RobotTextSession(new Input(actions),()=>true,feedback))
        {
            feedback.Expect(1,"start"); Check((await session.RecognizeAsync(Speech(1,"开启语音输入"))).Ok,"actual start");
            feedback.Expect(2,"type"); Check((await session.RecognizeAsync(Speech(2,"测试正文。"))).Ok,"actual body");
            var tool=new RobotTools(session);
            await tool.VoiceInputAsync("send","发送");
            await tool.VoiceInputAsync("stop");
            await tool.VoiceInputAsync("type","发送");
            await tool.VoiceInputAsync("type","退出语音输入");
            Check(session.Listening && actions.Count(x=>x=="input")==1 && !actions.Contains("enter") && !actions.Contains("stop"),"model cannot submit or exit a live draft");
        }
        Console.WriteLine("PASS model-forged send/exit/type commands have no native side effects and do not cancel the draft");
        actions=[]; feedback=new Feedback(actions);
        await using(var session=new RobotTextSession(new Input(actions),()=>true,feedback))
        {
            feedback.Expect(1,"start"); await session.RecognizeAsync(Speech(1,"开启 Codex 语音输入"));
            feedback.Expect(2,"type"); await session.RecognizeAsync(Speech(2,"增加发送按钮"));
            Check(!actions.Contains("enter"),"embedded send words remain body");
            feedback.Expect(3,"send"); Check((await session.RecognizeAsync(Speech(3,"发送。"))).Ok,"actual send");
            Check(actions.IndexOf("authorize:send")<actions.IndexOf("enter") && actions.IndexOf("enter")<actions.IndexOf("complete:send"),"hardware authorization must precede Enter");
            Check(!(await session.RecognizeAsync(Speech(3,"发送。"))).Ok,"replayed STT ignored");
            feedback.Expect(4,"send"); await session.RecognizeAsync(Speech(4,"发送"));
            Check(actions.Count(x=>x=="enter")==1 && actions.Contains("complete:skip") && session.Listening,"empty/repeated send stays active without another Enter");
            feedback.Expect(5,"stop"); await session.RecognizeAsync(Speech(5,"退出语音输入"));
            Check(!session.Listening && actions.Count(x=>x=="stop")==1,"actual exit works once");
        }
        Console.WriteLine("PASS only actual device send can press Enter, duplicate/empty sends do not replay, actual exit restores chat");
        actions=[];feedback=new Feedback(actions);
        await using(var session=new RobotTextSession(new Input(actions),()=>true,feedback))
        {
            feedback.Expect(1,"start"); await session.RecognizeAsync(Speech(1,"开启语音输入"));
            feedback.Expect(2,"type"); await session.RecognizeAsync(Speech(2,"草稿"));
            feedback.Expect(3,"type"); // Independent hardware transcript says body, while a caller claims send.
            Check(!(await session.RecognizeAsync(Speech(3,"发送"))).Ok,"mismatched device authorization rejected");
            Check(!actions.Contains("enter"),"failed authorization must precede any native send");
        }
        Console.WriteLine("PASS inconsistent or stale hardware turn authorization blocks Enter before injection");
        actions=[];feedback=new Feedback(actions);
        await using(var session=new RobotTextSession(new Input(actions),()=>true,feedback))
        await using(var dispatch=new RobotSpeechDispatcher(session,s=>s.Connection=="device-connection"))
        {
            feedback.Expect(1,"start");dispatch.Accept(new("old-connection",1,"开启语音输入"));
            dispatch.Accept(Speech(1,"开启语音输入")); await Wait(()=>session.Listening);
            feedback.Expect(2,"type");dispatch.Accept(Speech(2,"测试正文。"));dispatch.Accept(Speech(2,"测试正文。"));
            await Wait(()=>actions.Contains("complete:type"));
            await Task.Delay(50);
            Check(actions.Count(x=>x=="input")==1 && !actions.Contains("enter"),"old/replayed speech is not injected");
        }
        Console.WriteLine("PASS bounded speech dispatch ignores old connections and duplicate STT frames");
        actions=[];feedback=new Feedback(actions);
        await using(var session=new RobotTextSession(new Input(actions),()=>true,feedback))
        {
            long turn=0;
            for(var cycle=0;cycle<2;cycle++)
            {
                feedback.Expect(++turn,"start");Check((await session.RecognizeAsync(Speech(turn,"开启语音输入"))).Ok,"reopen on same connection");
                feedback.Expect(++turn,"type");Check((await session.RecognizeAsync(Speech(turn,"连续两轮测试"))).Ok,"body after reopen");
                await session.ExecuteAsync("send","发送");await session.ExecuteAsync("stop");
                Check(session.Listening,"model cannot terminate repeated cycle");
                feedback.Expect(++turn,"stop");Check((await session.RecognizeAsync(Speech(turn,"退出语音输入"))).Ok && !session.Listening,"explicit stop for each cycle");
            }
            Check(actions.Count(a=>a=="input")==2 && !actions.Contains("enter") && actions.Count(a=>a=="stop")==2,"repeated cycles preserve drafts and require a new start");
        }
        Console.WriteLine("PASS two start/body/stop cycles on one USB connection without reconnecting or automatic submission");
        actions=[];feedback=new Feedback(actions);
        await using(var session=new RobotTextSession(new Input(actions),()=>true,feedback))
        {
            feedback.Expect(1,"start");await session.RecognizeAsync(Speech(1,"打开语音输入"));
            long turn=1;
            foreach(var body in new[]{"帮我查看电量","这个函数退出模式后保存","发，送","拍照功能需要增加按钮"})
            {
                Check(RobotTextSession.Command(body)=="type","ordinary feature words or embedded commands remain dictation");
                feedback.Expect(++turn,"type");Check((await session.RecognizeAsync(Speech(turn,body))).Ok,"ordinary body typed");
            }
            Check(session.Listening && !actions.Contains("enter") && !actions.Contains("stop"),"feature words never send or exit");
            feedback.Expect(++turn,"stop");Check((await session.RecognizeAsync(Speech(turn,"退出模式。"))).Ok && !session.Listening,"exact exit mode phrase");
        }
        Console.WriteLine("PASS feature requests and embedded exit/send phrases stay text; only the exact exit phrase leaves mode");
        actions=[];feedback=new Feedback(actions);
        await using(var session=new RobotTextSession(new Input(actions),()=>true,feedback))
        {
            feedback.Expect(1,"start");await session.RecognizeAsync(Speech(1,"开启语音输入"));
            feedback.Expect(2,"type");await session.RecognizeAsync(Speech(2,"已存在的草稿"));
            session.Pause();Check(session.Listening && session.Paused,"pause must retain mode");
            session.ModelTransportReset();Check(session.Listening && session.Paused,"MCP reconnect must not clear mode");
            await session.ExecuteAsync("stop");Check(session.Listening,"model cannot exit paused mode");
            feedback.Expect(3,"type");Check((await session.RecognizeAsync(Speech(3,"连接恢复后的新正文"))).Ok && !session.Paused,"fresh authorized device speech resumes retained mode");
            Check(actions.Count(a=>a=="input")==2 && !actions.Contains("enter"),"no old body replay or auto send");
            session.Pause();Check(session.Listening && session.Paused,"mode retained for direct exit from pause");
            feedback.Expect(4,"stop");await session.RecognizeAsync(Speech(4,"退出模式"));Check(!session.Listening,"explicit exit clears retained mode");
        }
        Console.WriteLine("PASS transport changes and faults pause without exiting; fresh speech resumes and explicit exit clears mode");
        actions=[];feedback=new Feedback(actions);var blocking=new BlockingInput();
        await using(var session=new RobotTextSession(blocking,()=>true,feedback))
        await using(var dispatch=new RobotSpeechDispatcher(session,s=>s.Connection=="device-connection"))
        {
            var recognized=0;session.Audit+=(stage,_)=>{if(stage=="device_recognized") Interlocked.Increment(ref recognized);};
            feedback.Expect(1,"start");dispatch.Accept(Speech(1,"开启语音输入"));await Wait(()=>session.Listening);
            feedback.Expect(2,"type");dispatch.Accept(Speech(2,"正在输入的旧正文"));await blocking.Entered.Task.WaitAsync(TimeSpan.FromSeconds(2));
            dispatch.Accept(Speech(3,"暂停前已排队的旧正文"));session.Pause();blocking.Block=false;
            feedback.Expect(4,"type");dispatch.Accept(Speech(4,"暂停后的新正文"));await Wait(()=>blocking.Text.Count==1);await Task.Delay(50);
            Check(blocking.Text.Single()=="暂停后的新正文" && recognized==3,"queued pre-pause utterance must never resume or replay");
        }
        Console.WriteLine("PASS pausing invalidates queued old utterances while retaining fresh explicit speech");
        var wire=new Wire();
        await using(var link=new UsbRobotFeedback(()=>wire))
        {
            RobotSpeech? captured=null;link.Speech+=s=>captured=s;
            await Wait(()=>link.Connected);
            wire.Emit("测试正文。增加发送按钮");
            await Wait(()=>captured is not null);
            Check(captured!.Text=="测试正文。增加发送按钮" && captured.Turn==7 && captured.Connection==link.Connection,"UTF-8 raw STT preserved independently of model arguments");
            await link.PrepareSpeechAsync("type",7,default);
            Check(wire.Operations.Contains("authorize_type:7"),"turn retained on actual wire");
        }
        Console.WriteLine("PASS direct UTF-8 speech events and correlated authorization survive the real USB protocol parser");
        return 9;
    }
    private sealed class Input(List<string> actions):IRobotInput
    {
        public Task AppendAsync(string text,CancellationToken token){actions.Add("input");return Task.CompletedTask;}
        public Task SendAsync(CancellationToken token){actions.Add("enter");return Task.CompletedTask;}
    }
    private sealed class BlockingInput:IRobotInput
    {
        public volatile bool Block=true;
        public readonly TaskCompletionSource Entered=new(TaskCreationOptions.RunContinuationsAsynchronously);
        public readonly ConcurrentQueue<string> Text=new();
        public async Task AppendAsync(string text,CancellationToken token)
        { if(Block){Entered.TrySetResult();await Task.Delay(Timeout.Infinite,token);}token.ThrowIfCancellationRequested();Text.Enqueue(text); }
        public Task SendAsync(CancellationToken token)=>throw new Exception("unexpected send");
    }
    private sealed class Feedback(List<string> actions):IRobotSpeechFeedback
    {
        private long expectedTurn;private string expectedAction="";
        public void Expect(long turn,string action){expectedTurn=turn;expectedAction=action;}
        public Task SetSpeechModeAsync(bool active,long turn,CancellationToken token)
        { Validate(active?"start":"stop",turn);actions.Add(active?"start":"stop");return Task.CompletedTask; }
        public Task PrepareSpeechAsync(string action,long turn,CancellationToken token)
        { Validate(action,turn);actions.Add("authorize:"+action);return Task.CompletedTask; }
        public Task CompleteSpeechAsync(string action,long turn,CancellationToken token)
        { Validate(action=="skip"?"send":action,turn);actions.Add("complete:"+action);return Task.CompletedTask; }
        private void Validate(string action,long turn){if(action!=expectedAction||turn!=expectedTurn)throw new IOException("independent hardware denied");}
        public Task SetModeAsync(bool active,CancellationToken token)=>throw new Exception("legacy mode API must not authorize speech");
        public Task CompleteAsync(string action,CancellationToken token)=>throw new Exception("legacy completion API must not authorize speech");
        public void Reset(){actions.Add("abort");}
    }
    private sealed class Wire:IRobotFeedbackWire
    {
        private readonly BlockingCollection<string> lines=new();private string session="";public readonly ConcurrentQueue<string> Operations=new();
        public void WriteLine(string line)
        {
            using var json=JsonDocument.Parse(line[6..]);var r=json.RootElement;session=r.GetProperty("session").GetString()!;
            var op=r.GetProperty("op").GetString();var turn=r.GetProperty("turn").GetInt64();Operations.Enqueue(op+":"+turn);
            lines.Add("@CVI1 "+JsonSerializer.Serialize(new{v=1,session,id=r.GetProperty("id").GetInt32(),board="esp-sparkbot",speech=true,mode_lock=true,ok=true}));
        }
        public void Emit(string text)=>lines.Add("@CVI1 "+JsonSerializer.Serialize(new{v=1,session,id=0,board="esp-sparkbot",@event="speech",turn=7,text}));
        public string? ReadLine()=>lines.TryTake(out var line,50)?line:null;
        public void Dispose(){}
    }
}