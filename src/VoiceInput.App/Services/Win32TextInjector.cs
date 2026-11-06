using System.ComponentModel;
using System.Runtime.InteropServices;

namespace VoiceInput.App.Services;

public static class Win32TextInjector
{
    public static void TypeText(string text)
    {
        if (string.IsNullOrEmpty(text)) return;

        var inputs = new List<Input>(text.Length * 2);
        foreach (var ch in text)
        {
            inputs.Add(Input.UnicodeKey(ch, keyUp: false));
            inputs.Add(Input.UnicodeKey(ch, keyUp: true));
        }

        var sent = SendInput((uint)inputs.Count, inputs.ToArray(), Marshal.SizeOf<Input>());
        if (sent != inputs.Count)
        {
            var nativeError = Marshal.GetLastPInvokeError();
            throw new Win32Exception(
                nativeError,
                $"SendInput sent {sent} of {inputs.Count} events (INPUT size {Marshal.SizeOf<Input>()}).");
        }
    }

    public static void SendEnter()
    {
        var values = new[] { new Input { Type = 1, Union = new InputUnion
            { Keyboard = new KeyboardInput { VirtualKey = 0x0D } } },
            new Input { Type = 1, Union = new InputUnion
            { Keyboard = new KeyboardInput { VirtualKey = 0x0D, Flags = 0x0002 } } } };
        var sent = SendInput(2, values, Marshal.SizeOf<Input>());
        if (sent != 2)
        {
            SendInput(1, new[] { values[1] }, Marshal.SizeOf<Input>());
            throw new Win32Exception(Marshal.GetLastPInvokeError(), "Send key was not fully injected; check the screen.");
        }
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint cInputs, Input[] pInputs, int cbSize);

    [StructLayout(LayoutKind.Sequential)]
    private struct Input
    {
        public uint Type;
        public InputUnion Union;

        public static Input UnicodeKey(char ch, bool keyUp)
        {
            return new Input
            {
                Type = 1,
                Union = new InputUnion
                {
                    Keyboard = new KeyboardInput
                    {
                        VirtualKey = 0,
                        ScanCode = ch,
                        Flags = keyUp ? 0x0004u | 0x0002u : 0x0004u,
                    },
                },
            };
        }
    }

    [StructLayout(LayoutKind.Explicit, Size = 32)]
    private struct InputUnion
    {
        [FieldOffset(0)]
        public KeyboardInput Keyboard;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardInput
    {
        public ushort VirtualKey;
        public ushort ScanCode;
        public uint Flags;
        public uint Time;
        public nint ExtraInfo;
    }
}
