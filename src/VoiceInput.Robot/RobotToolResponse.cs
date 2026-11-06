using System.Text.Json;
using ModelContextProtocol.Protocol;

namespace VoiceInput.Robot;

/// <summary>Keep machine status separate from the short text intended for the robot's voice.</summary>
public static class RobotToolResponse
{
    public static CallToolResult Create(string action, string? text, RobotResult result)
    {
        action = action.Trim().ToLowerInvariant();
        if (action == "type")
        {
            var command = RobotTextSession.Command(text ?? "");
            if (command != "type") action = command;
        }

        var reply = !result.Ok || action == "status" ? result.Message
            : action == "start" ? "语音输入已开启" : action == "stop" ? "已退出语音输入" : "";
        var policy = !result.Ok ? "error"
            : action == "status" ? "status"
            : action is "start" or "stop" ? "brief_mode" : "silent";

        return new CallToolResult
        {
            IsError = !result.Ok,
            Content = [new TextContentBlock { Text = reply }],
            StructuredContent = JsonSerializer.SerializeToElement(new
            {
                ok = result.Ok,
                active = result.Active,
                message = result.Message,
                action,
                replyPolicy = policy
            })
        };
    }
}
