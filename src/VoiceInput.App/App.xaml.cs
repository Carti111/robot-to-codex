using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using VoiceInput.App.Services;

namespace VoiceInput.App;

public partial class App : Application
{
    private MainWindow? window;

    public App()
    {
        InitializeComponent();
        UnhandledException += (_, args) => AppDiagnostics.Error("Unhandled UI exception", args.Exception);
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        try
        {
            _ = SetCurrentProcessExplicitAppUserModelID("Xiaozhi.CodexVoiceInputRobot");
            window = new MainWindow();
            window.Closed += (_, _) => window.Dispose();
            ApplicationInstance.Listen(() => window.DispatcherQueue.TryEnqueue(() =>
            {
                AppDiagnostics.Info("Settings activated by application launch.");
                window.OpenSettingsForDiagnostics();
            }));
            window.Activate();
            window.HideHostWindow();
            window.OpenSettingsForDiagnostics();
        }
        catch (Exception error)
        {
            AppDiagnostics.Error("Application startup failed", error);
            throw;
        }
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern int SetCurrentProcessExplicitAppUserModelID(string appId);
}
