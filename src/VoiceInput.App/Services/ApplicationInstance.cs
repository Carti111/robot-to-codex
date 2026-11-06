namespace VoiceInput.App.Services;

/// <summary>Claim application ownership before creating any WinUI application or window.</summary>
internal static class ApplicationInstance
{
    private const string MutexName = @"Local\CodexVoiceInputRobot.SingleInstance";
    private const string EventName = @"Local\CodexVoiceInputRobot.ShowSettings";
    private static Mutex? mutex;
    private static EventWaitHandle? showSettings;
    private static RegisteredWaitHandle? listener;
    private static bool ownsMutex;
    private static volatile bool closing;

    public static bool ClaimOrActivate()
    {
        showSettings = new EventWaitHandle(false, EventResetMode.AutoReset, EventName);
        mutex = new Mutex(true, MutexName, out ownsMutex);
        if (ownsMutex) return true;
        showSettings.Set();
        showSettings.Dispose();
        mutex.Dispose();
        showSettings = null;
        mutex = null;
        AppDiagnostics.Info("Existing application requested to show settings.");
        return false;
    }

    public static void Listen(Action activate)
    {
        listener = ThreadPool.RegisterWaitForSingleObject(showSettings!, (_, _) =>
        {
            if (!closing)
                try { activate(); } catch { /* The UI may already be shutting down. */ }
        }, null, Timeout.Infinite, false);
    }

    public static void Close()
    {
        closing = true;
        listener?.Unregister(null);
        showSettings?.Dispose();
        if (ownsMutex)
            try { mutex?.ReleaseMutex(); } catch (ApplicationException) { }
        mutex?.Dispose();
    }
}
