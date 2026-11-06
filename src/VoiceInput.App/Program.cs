using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using VoiceInput.App.Services;

namespace VoiceInput.App;

public static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        // A repeat launch only signals the existing instance; it never starts WinUI or a cloud bridge.
        if (!ApplicationInstance.ClaimOrActivate()) return 0;
        try
        {
            WinRT.ComWrappersSupport.InitializeComWrappers();
            Application.Start(initialization =>
            {
                SynchronizationContext.SetSynchronizationContext(
                    new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread()));
                _ = new App();
            });
            return 0;
        }
        finally { ApplicationInstance.Close(); }
    }
}
