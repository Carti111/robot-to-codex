using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;
using VoiceInput.Robot;

namespace VoiceInput.App.Services;

/// <summary>Robot-only input uses the upstream injector, with a foreground Codex check.</summary>
public sealed class RobotCodexInput(DispatcherQueue dispatcher) : IRobotInput
{
    public Task AppendAsync(string text, CancellationToken cancellationToken) =>
        OnUiAsync(() => Win32TextInjector.TypeText(text.ReplaceLineEndings(" ")), cancellationToken);

    public Task SendAsync(CancellationToken cancellationToken) =>
        OnUiAsync(Win32TextInjector.SendEnter, cancellationToken);

    private Task OnUiAsync(Action action, CancellationToken token)
    {
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!dispatcher.TryEnqueue(() =>
        {
            try
            {
                token.ThrowIfCancellationRequested();
                var window = GetForegroundWindow();
                GetWindowThreadProcessId(window, out var pid);
                using var process = Process.GetProcessById((int)pid);
                var isCodex = process.ProcessName.Equals("Codex", StringComparison.OrdinalIgnoreCase)
                    || (process.ProcessName.Equals("ChatGPT", StringComparison.OrdinalIgnoreCase)
                        && (process.MainModule?.FileName?.Contains("OpenAI.Codex_", StringComparison.OrdinalIgnoreCase) ?? false));
                if (!isCodex || window == 0 || GetForegroundWindow() != window)
                    throw new InvalidOperationException("请切到 Codex，并点击目标输入框。");
                action();
                completion.TrySetResult();
            }
            catch (OperationCanceledException) { completion.TrySetCanceled(token); }
            catch (Exception error) { completion.TrySetException(error); }
        })) completion.TrySetException(new ObjectDisposedException("Codex Voice Input"));
        return completion.Task;
    }

    [DllImport("user32.dll")]
    private static extern nint GetForegroundWindow();
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint window, out uint processId);
}
