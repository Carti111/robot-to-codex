using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using VoiceInput.Robot;

namespace VoiceInput.App.Services;

/// <summary>Windows user-bound DPAPI. URLs are never stored in config.json or diagnostic messages.</summary>
public static class RobotEndpointStore
{
    public static string FilePath => Path.Combine(AppConfig.DirectoryPath, "secrets", "xiaozhi-endpoint.bin");
    public static string? Load(string? filePath = null)
    {
        filePath ??= FilePath;
        if (!File.Exists(filePath)) return null;
        try
        {
            var value = Encoding.UTF8.GetString(Transform(File.ReadAllBytes(filePath), false));
            return RobotCloudConnection.TryEndpoint(value, out _) ? value : null;
        }
        catch { return null; }
    }

    public static void Save(string? endpoint, string? filePath = null)
    {
        filePath ??= FilePath;
        if (endpoint is null) { if (File.Exists(filePath)) File.Delete(filePath); return; }
        if (!RobotCloudConnection.TryEndpoint(endpoint, out _))
            throw new ArgumentException("接入点格式不正确。");
        var data = Transform(Encoding.UTF8.GetBytes(endpoint), true);
        Directory.CreateDirectory(Path.GetDirectoryName(filePath)!);
        var temporary = filePath + ".tmp";
        File.WriteAllBytes(temporary, data);
        File.Move(temporary, filePath, true);
    }

    public static bool ManagerRunning()
    {
        try { using var mutex = Mutex.OpenExisting(@"Local\XiaozhiMcpManagerRobot"); return true; }
        catch (WaitHandleCannotBeOpenedException) { return false; }
        catch { return true; } // Cannot establish ownership: do not compete for the endpoint.
    }

    internal static byte[] Transform(byte[] bytes, bool protect)
    {
        var input = new Blob { Size = bytes.Length, Data = Marshal.AllocHGlobal(bytes.Length) };
        try
        {
            Marshal.Copy(bytes, 0, input.Data, bytes.Length);
            Blob output;
            var success = protect
                ? CryptProtectData(ref input, "Codex Voice Input Robot", 0, 0, 0, 1, out output)
                : CryptUnprotectData(ref input, 0, 0, 0, 0, 1, out output);
            if (!success) throw new Win32Exception(Marshal.GetLastWin32Error());
            try { var result = new byte[output.Size]; Marshal.Copy(output.Data, result, 0, result.Length); return result; }
            finally { LocalFree(output.Data); }
        }
        finally { Marshal.FreeHGlobal(input.Data); }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Blob { public int Size; public nint Data; }
    [DllImport("crypt32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptProtectData(ref Blob input, string description, nint entropy,
        nint reserved, nint prompt, uint flags, out Blob output);
    [DllImport("crypt32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptUnprotectData(ref Blob input, nint description, nint entropy,
        nint reserved, nint prompt, uint flags, out Blob output);
    [DllImport("kernel32.dll")]
    private static extern nint LocalFree(nint memory);
}
