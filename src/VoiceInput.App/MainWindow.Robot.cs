using VoiceInput.App.Services;
using VoiceInput.Robot;

namespace VoiceInput.App;

public sealed partial class MainWindow
{
    private UsbRobotFeedback? robotUsb; // Shared CVI1 protocol engine; daily wire is TLS Wi-Fi only.
    private WifiRobotServer? robotWifi;
    private RobotWifiIdentity robotWifiIdentity=null!;
    private System.Security.Cryptography.X509Certificates.X509Certificate2? robotWifiCertificate;
    private RobotSpeechDispatcher? robotSpeech;
    private RobotTextSession robotSession=null!;
    private bool robotClosed;
    private string robotLinkCode="waiting_robot";

    private void InitializeRobot()
    {
        robotWifiIdentity=RobotWifiIdentityStore.LoadOrCreate();
        robotWifiCertificate=robotWifiIdentity.Certificate();
        robotWifi=new WifiRobotServer(robotWifiCertificate,robotWifiIdentity.Token,robotWifiIdentity.Identity);
        robotUsb=new UsbRobotFeedback(robotWifi.AcceptWire,requireLocalMode:true);
        robotSession=new RobotTextSession(new RobotCodexInput(DispatcherQueue),()=>!robotClosed,robotUsb);
        robotSpeech=new RobotSpeechDispatcher(robotSession,speech=>!robotClosed&&robotUsb.Connected&&speech.Connection==robotUsb.Connection);
        robotUsb.Speech+=robotSpeech.Accept;
        robotSession.Audit+=(stage,action)=>AppDiagnostics.Info("Robot action "+stage+": "+action);
        robotUsb.Changed+=state=>{
            robotLinkCode=state.Code;
            AppDiagnostics.Info("Robot Wi-Fi feedback: "+state.Code);
            if(state.Code=="chat"&&robotSession.Listening)robotSession.SetMode(false,resetFeedback:false);
            if(state.Code=="paused")robotSession.Pause("机器人语音会话已暂停，输入模式仍保留。",notifyFeedback:false);
            if(!state.Connected&&robotSession.Listening)robotSession.Pause("机器人无线连接中断，输入已暂停，模式仍保留。",notifyFeedback:false);
            DispatcherQueue.TryEnqueue(()=>{
                if(!robotClosed&&!robotSession.Listening)UpdateRobotMessage(RobotConnectionStatus().Message);
            });
        };
        robotSession.Changed+=OnRobotNotice;
        AppDiagnostics.Info("Robot input uses native firmware and paired Wi-Fi; no MCP connection started.");
    }
    private void OnRobotNotice(RobotNotice notice)
    {
        DispatcherQueue.TryEnqueue(()=>{
            if(robotClosed)return;
            if(!string.IsNullOrWhiteSpace(notice.Text))historyStore.Add(notice.Text);
            UpdateRobotMessage(notice.Message,notice.Error);
        });
    }
    private RobotConnectionState RobotConnectionStatus()=>robotUsb?.Connected==true
        ?new("ready",robotSession.Paused?"已连接 · 听写已暂停":"已连接")
        :robotLinkCode.Contains("robot_native_mode_required",StringComparison.Ordinal)?new("firmware_required","机器人固件需要更新"):new("waiting_robot","等待机器人无线连接");
    private async Task PairRobotAsync(PairableRobot robot,string code)
    {
        if(robotClosed||robotSession.Listening||robotSession.Pending>0)
            throw new InvalidOperationException("请先退出语音输入，再进行配对。");
        using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(35));
        var provision=new WifiPairingProvision(robotWifiCertificate!.ExportCertificatePem(),robotWifiIdentity.Token,
            robotWifiIdentity.Identity,WifiRobotPairing.LocalAddressFor(robot.Address));
        await WifiRobotPairing.PairAsync(robot,code,provision,timeout.Token);
    }
    private async Task CloseRobotAsync()
    {
        if(robotSpeech is not null)await robotSpeech.DisposeAsync();
        await robotSession.DisposeAsync();
        if(robotUsb is not null)await robotUsb.DisposeAsync();
        if(robotWifi is not null)await robotWifi.DisposeAsync();
        robotWifiCertificate?.Dispose();
    }
}
