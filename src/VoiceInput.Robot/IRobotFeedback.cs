namespace VoiceInput.Robot;

/// <summary>Device feedback is independent of text injection and never plays computer audio.</summary>
public interface IRobotFeedback
{
    Task SetModeAsync(bool active, CancellationToken cancellationToken);
    Task CompleteAsync(string action, CancellationToken cancellationToken);
    void Reset();
}

public sealed record RobotSpeech(string Connection, long Turn, string Text);

/// <summary>Only firmware STT events may authorize an input operation. MCP arguments never grant this authority.</summary>
public interface IRobotSpeechFeedback : IRobotFeedback
{
    Task SetSpeechModeAsync(bool active, long turn, CancellationToken token);
    Task PrepareSpeechAsync(string action, long turn, CancellationToken token);
    Task CompleteSpeechAsync(string action, long turn, CancellationToken token);
}
