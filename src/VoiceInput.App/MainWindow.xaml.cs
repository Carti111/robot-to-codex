using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using VoiceInput.App.Services;
using WinRT.Interop;

namespace VoiceInput.App;

public sealed partial class MainWindow : Window, IDisposable
{
    private readonly AppConfig config;
    private readonly RecognitionHistoryStore historyStore;
    private readonly Win32TrayIcon trayIcon;
    private readonly nint hwnd;
    private SettingsWindow? settingsWindow;
    private bool disposed;
    private string robotMessage="";
    private bool robotMessageError;

    public MainWindow()
    {
        InitializeComponent();
        config=AppConfig.Load();
        historyStore=new RecognitionHistoryStore();
        hwnd=WindowNative.GetWindowHandle(this);
        var appWindow=AppWindow.GetFromWindowId(Win32Interop.GetWindowIdFromWindow(hwnd));
        appWindow.Title="Codex Voice Input · 机器人版";
        appWindow.Resize(new Windows.Graphics.SizeInt32(1,1));
        appWindow.SetIcon(Path.Combine(AppContext.BaseDirectory,"Assets","codex-voice-input.ico"));
        SetWindowLong(hwnd,-20,GetWindowLong(hwnd,-20)|0x00000080|0x08000000);
        trayIcon=new Win32TrayIcon(DispatcherQueue,ShowSettingsWindow,OpenLogFile,ExitApplication);
        try {trayIcon.Start();}catch(Exception error){AppDiagnostics.Error("Tray icon failed",error);}
        InitializeRobot();
        AppDiagnostics.Info("Robot-only application started: no computer microphone, floating window or global hotkey.");
    }
    private void UpdateRobotMessage(string message,bool error=false)
    {
        robotMessage=message;robotMessageError=error;
        trayIcon.SetTooltip("Codex Voice Input - "+message);
        settingsWindow?.UpdateRobotStatus(message,error);
    }
    public void HideHostWindow()=>ShowWindow(hwnd,0);
    private void ShowSettingsWindow()
    {
        if(disposed)return;
        if(settingsWindow is not null){settingsWindow.Activate();return;}
        settingsWindow=new SettingsWindow(config,_=>null,historyStore,RobotConnectionStatus,PairRobotAsync,ExitApplication);
        settingsWindow.Closed+=(_,_)=>settingsWindow=null;
        settingsWindow.UpdateRobotStatus(robotMessage,robotMessageError);
        settingsWindow.Activate();
    }
    internal void OpenSettingsForDiagnostics()=>ShowSettingsWindow();
    private void OpenLogFile()=>System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo {
        FileName=AppDiagnostics.FilePath,UseShellExecute=true
    });
    private async void ExitApplication()
    {
        if(disposed)return;
        disposed=true;robotClosed=true;
        try {await CloseRobotAsync();}catch(Exception error){AppDiagnostics.Error("Robot shutdown",error);}
        settingsWindow?.Close();trayIcon.Dispose();App.Current.Exit();
    }
    public void Dispose()
    {
        if(disposed)return;
        disposed=true;robotClosed=true;
        settingsWindow?.Close();trayIcon.Dispose();_=CloseRobotAsync();
    }
    [DllImport("user32.dll")]private static extern bool ShowWindow(nint hwnd,int command);
    [DllImport("user32.dll")]private static extern int GetWindowLong(nint hwnd,int index);
    [DllImport("user32.dll")]private static extern int SetWindowLong(nint hwnd,int index,int value);
}
