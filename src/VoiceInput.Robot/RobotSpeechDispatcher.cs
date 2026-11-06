using System.Threading.Channels;
namespace VoiceInput.Robot;

/// <summary>Do not block the serial reader while a recognized utterance waits for a device authorization.</summary>
public sealed class RobotSpeechDispatcher : IAsyncDisposable
{
    private readonly RobotTextSession session;
    private readonly Func<RobotSpeech, bool> current;
    private sealed record PendingSpeech(RobotSpeech Speech, int Epoch);
    private readonly Channel<PendingSpeech> queue = Channel.CreateBounded<PendingSpeech>(8);
    private readonly CancellationTokenSource lifetime = new();
    private readonly Task worker;
    public RobotSpeechDispatcher(RobotTextSession session, Func<RobotSpeech, bool> current)
    { this.session = session; this.current = current; worker = Task.Run(RunAsync); }
    public void Accept(RobotSpeech speech)
    { if (!lifetime.IsCancellationRequested && current(speech) && !queue.Writer.TryWrite(new(speech, session.InputEpoch))) session.Pause(); }
    private async Task RunAsync()
    {
        try
        {
            await foreach (var pending in queue.Reader.ReadAllAsync(lifetime.Token))
            {
                var speech = pending.Speech;
                if (!current(speech)) continue;
                if (pending.Epoch != session.InputEpoch && RobotTextSession.Command(speech.Text) is not ("start" or "stop")) continue;
                try { await session.RecognizeAsync(speech, lifetime.Token); }
                catch (OperationCanceledException) { }
                catch { session.Pause(); }
            }
        }
        catch (OperationCanceledException) { }
    }
    public async ValueTask DisposeAsync()
    { lifetime.Cancel(); queue.Writer.TryComplete(); await worker; lifetime.Dispose(); }
}
