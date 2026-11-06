using VoiceInput.Robot;

// No networking, ASR, keyboard injection or real robot. All sentences below are synthetic fixtures.
var device=new SimulatedDevice();
await using var session=new RobotTextSession(new ConsoleDraft(),()=>true,device);
long turn=0;
foreach(var (intent,text) in new[]{("start","开启语音输入"),("type","帮我给这个函数增加日志"),("send","发送"),("type","这个函数退出模式后保存"),("stop","退出模式")}) {
    device.FinalStt(++turn,intent);
    var result=await session.RecognizeAsync(new("example-session",turn,text));
    if(!result.Ok)throw new Exception("Example failed: "+result.Message);
}
Console.WriteLine("Done. No text or Enter was sent to any application.");

sealed class ConsoleDraft : IRobotInput {
    public Task AppendAsync(string text,CancellationToken token){token.ThrowIfCancellationRequested();Console.WriteLine("DRAFT: "+text);return Task.CompletedTask;}
    public Task SendAsync(CancellationToken token){token.ThrowIfCancellationRequested();Console.WriteLine("SEND (console simulation only)");return Task.CompletedTask;}
}
sealed class SimulatedDevice : IRobotSpeechFeedback {
    private long currentTurn;private string intent="";private bool authorized;
    public void FinalStt(long turn,string action){currentTurn=turn;intent=action;authorized=false;}
    private void Check(long turn,string action){if(turn!=currentTurn||intent!=action)throw new IOException("stale_or_mismatched_turn");}
    public Task SetSpeechModeAsync(bool active,long turn,CancellationToken token){token.ThrowIfCancellationRequested();Check(turn,active?"start":"stop");Console.WriteLine(active?"DEVICE: mode on":"DEVICE: mode off");return Task.CompletedTask;}
    public Task PrepareSpeechAsync(string action,long turn,CancellationToken token){token.ThrowIfCancellationRequested();Check(turn,action);if(authorized)throw new IOException("already_authorized");authorized=true;Console.WriteLine("DEVICE: authorize "+action);return Task.CompletedTask;}
    public Task CompleteSpeechAsync(string action,long turn,CancellationToken token){token.ThrowIfCancellationRequested();Check(turn,action=="skip"?"send":action);if(!authorized)throw new IOException("not_authorized");authorized=false;Console.WriteLine("DEVICE: complete "+action+(action=="type"?"; one robot cue":"; silent"));return Task.CompletedTask;}
    public Task SetModeAsync(bool active,CancellationToken token)=>throw new NotSupportedException("Use real STT turns");
    public Task CompleteAsync(string action,CancellationToken token)=>throw new NotSupportedException("Use real STT turns");
    public void Reset(){authorized=false;}
}
