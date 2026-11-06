using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using VoiceInput.App.Services;
using VoiceInput.Robot;
using WinRT.Interop;

namespace VoiceInput.App;

public sealed partial class SettingsWindow : Window
{

    private readonly AppConfig config;
    private readonly Func<AppConfig, string?> settingsApplied;
    private readonly RecognitionHistoryStore historyStore;
    private readonly Func<RobotConnectionState> connectionState;
    private readonly Func<PairableRobot,string,Task> pairRobot;
    private bool pairingBusy;
    private CancellationTokenSource? pairingSearch;
    private readonly Action exitApplication;
    private readonly AppWindow appWindow;
    private readonly DispatcherTimer statusTimer = new() { Interval = TimeSpan.FromSeconds(1) };
    private bool loaded;
    private bool previewing = false;

    public SettingsWindow(AppConfig config, Func<AppConfig, string?> settingsApplied,
        RecognitionHistoryStore historyStore, Func<RobotConnectionState> connectionState,
        Func<PairableRobot,string,Task> pairRobot, Action exitApplication)
    {
        this.config = config;
        this.settingsApplied = settingsApplied;
        this.historyStore = historyStore;
        this.connectionState = connectionState;
        this.pairRobot = pairRobot;
        this.exitApplication = exitApplication;
        InitializeComponent();
        StatusText.RegisterPropertyChangedCallback(TextBlock.TextProperty, (_, _) =>
            StatusText.Visibility = string.IsNullOrWhiteSpace(StatusText.Text) ? Visibility.Collapsed : Visibility.Visible);
        var hwnd = WindowNative.GetWindowHandle(this);
        appWindow = AppWindow.GetFromWindowId(Win32Interop.GetWindowIdFromWindow(hwnd));
        ConfigureWindow(hwnd);
        appWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "codex-voice-input.ico"));
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(TitleBarDragRegion);
        try { SystemBackdrop = new DesktopAcrylicBackdrop(); } catch { }
        LoadPreferences();

        ApplyTheme();
        ReloadHistory();
        Navigation.SelectedIndex = 0;
        statusTimer.Tick += (_, _) => RefreshConnection();
        statusTimer.Start();
        RefreshConnection();
        historyStore.Changed += HistoryStore_Changed;
        Closed += (_, _) => { statusTimer.Stop(); pairingSearch?.Cancel(); historyStore.Changed -= HistoryStore_Changed; };
        loaded = true;
#if DEBUG
        _ = CapturePreviewsAsync();
#endif
    }

    private void ConfigureWindow(nint hwnd)
    {
        var scale = Math.Max(1, GetDpiForWindow(hwnd) / 96.0);
        var area = DisplayArea.GetFromWindowId(appWindow.Id, DisplayAreaFallback.Primary).WorkArea;
        var width = Math.Min((int)(820 * scale), area.Width - 32);
        var height = Math.Min((int)(690 * scale), area.Height - 32);
        appWindow.Resize(new Windows.Graphics.SizeInt32(width, height));
        appWindow.Move(new Windows.Graphics.PointInt32(area.X + (area.Width - width) / 2,
            area.Y + (area.Height - height) / 2));
        if (appWindow.Presenter is OverlappedPresenter presenter)
        { presenter.IsResizable = true; presenter.IsMaximizable = false; }
        var corner = 2;
        _ = DwmSetWindowAttribute(hwnd, 33, ref corner, sizeof(int));
    }

    private void LoadPreferences()
    {
        var wasLoaded = loaded;
        loaded = false;
        DarkModeToggle.IsOn = config.DarkMode;
        loaded = wasLoaded;
    }

    private async void AddRobot_Click(object sender, RoutedEventArgs e)
    {
        PairingEditor.Visibility=Visibility.Visible;
        await SearchRobots();
    }
    private async void SearchRobots_Click(object sender, RoutedEventArgs e)=>await SearchRobots();
    private async Task SearchRobots()
    {
        if(pairingBusy)return;
        pairingBusy=true;PairingSearchButton.IsEnabled=false;PairRobotButton.IsEnabled=false;
        PairingCodeBox.Text=string.Empty;
        pairingSearch?.Cancel();pairingSearch=new CancellationTokenSource(TimeSpan.FromSeconds(6));
        try {
            PairingStatus.Text="正在搜索…请触摸顶部约 3 秒，让屏幕显示配对码。";
            var found=await WifiRobotPairing.DiscoverAsync(pairingSearch.Token);
            PairingRobotsBox.ItemsSource=found;PairingRobotsBox.SelectedIndex=found.Count>0?0:-1;
            PairingStatus.Text=found.Count>0?"输入机器人屏幕上的六位配对码。":"未发现机器人。请确认已进入配对模式，并连接同一局域网。";
        }catch(OperationCanceledException){}
        catch {PairingStatus.Text="搜索未完成，请检查网络后重试。";}
        finally {pairingBusy=false;PairingSearchButton.IsEnabled=true;PairRobotButton.IsEnabled=true;}
    }
    private void CancelPairing_Click(object sender,RoutedEventArgs e)
    {
        pairingSearch?.Cancel();PairingCodeBox.Text=string.Empty;PairingEditor.Visibility=Visibility.Collapsed;
    }
    private async void PairRobot_Click(object sender,RoutedEventArgs e)
    {
        if(pairingBusy)return;
        if(PairingRobotsBox.SelectedItem is not PairableRobot robot) {PairingStatus.Text="请先搜索并选择机器人。";return;}
        var code=PairingCodeBox.Text.Trim();
        if(code.Length!=6||!code.All(c=>c is >= '0' and <= '9')) {PairingStatus.Text="请输入屏幕上的六位数字。";return;}
        pairingBusy=true;PairRobotButton.IsEnabled=PairingSearchButton.IsEnabled=CancelPairingButton.IsEnabled=false;
        try {
            PairingStatus.Text="正在安全配对…";
            await pairRobot(robot,code);
            PairingCodeBox.Text=string.Empty;PairingStatus.Text="配对成功，机器人正在自动连接。";
        }catch(InvalidOperationException error) {PairingStatus.Text=error.Message;}
        catch {PairingStatus.Text="配对未完成。请核对配对码；过期时重新长触顶部，再搜索重试。";}
        finally {pairingBusy=false;PairRobotButton.IsEnabled=PairingSearchButton.IsEnabled=CancelPairingButton.IsEnabled=true;RefreshConnection();}
    }

    private bool ApplyPreference(Action change, Action undo, string confirmation)
    {
        try
        {
            change();
            var error = settingsApplied(config);
            if (error is not null)
            {
                undo();
                LoadPreferences();
                StatusText.Text = error;
                return false;
            }
            config.Save();
            StatusText.Text = confirmation;
            RefreshConnection();
            return true;
        }
        catch
        {
            undo();
            try { _ = settingsApplied(config); config.Save(); } catch { }
            LoadPreferences();
            StatusText.Text = "设置未应用，请稍后重试。";
            return false;
        }
    }

    private void DarkMode_Toggled(object sender, RoutedEventArgs e)
    {
        if (!loaded || previewing) return;
        var previous = config.DarkMode;
        _ = ApplyPreference(() => config.DarkMode = DarkModeToggle.IsOn, () => config.DarkMode = previous, "外观已更新");
        ApplyTheme();
    }

    private void Navigation_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (RobotPage is null || GeneralPage is null || HistoryPage is null) return;
        var tag = (Navigation.SelectedItem as ListViewItem)?.Tag as string;
        RobotPage.Visibility = tag == "robot" ? Visibility.Visible : Visibility.Collapsed;
        GeneralPage.Visibility = tag == "general" ? Visibility.Visible : Visibility.Collapsed;
        HistoryPage.Visibility = tag == "history" ? Visibility.Visible : Visibility.Collapsed;
        if (loaded) StatusText.Text = string.Empty;
    }

    private void RefreshConnection()
    {
        var state = connectionState();
        ConnectionText.Text = state.Message;

        ConnectionDot.Fill = new SolidColorBrush(state.Ready
            ? Windows.UI.Color.FromArgb(255, 48, 158, 92)
            : state.Code is "manager" or "reconnecting"
                ? Windows.UI.Color.FromArgb(255, 195, 132, 37)
                : config.DarkMode ? Windows.UI.Color.FromArgb(255, 174, 174, 178)
                                  : Windows.UI.Color.FromArgb(255, 142, 142, 147));
    }

    public void UpdateRobotStatus(string message,bool error)
    {
        var show=error||(!string.IsNullOrWhiteSpace(message)&&message!="已连接"&&message!="等待机器人无线连接");
        RobotRuntimeText.Text=show?message:string.Empty;
        RobotRuntimeText.Visibility=show?Visibility.Visible:Visibility.Collapsed;
        RobotRuntimeText.Foreground=error?new SolidColorBrush(Windows.UI.Color.FromArgb(255,190,92,46)):new SolidColorBrush(config.DarkMode?Windows.UI.Color.FromArgb(255,174,174,178):Windows.UI.Color.FromArgb(255,110,110,115));
    }

    private void ApplyTheme()
    {
        Root.RequestedTheme = config.DarkMode ? ElementTheme.Dark : ElementTheme.Light;
        appWindow.TitleBar.ButtonBackgroundColor = Colors.Transparent;
        appWindow.TitleBar.ButtonInactiveBackgroundColor = Colors.Transparent;
        appWindow.TitleBar.ButtonForegroundColor = config.DarkMode ? Colors.White : Colors.Black;
    }

    private void HistoryStore_Changed(object? sender, EventArgs e) => DispatcherQueue.TryEnqueue(ReloadHistory);
    private void ReloadHistory()
    {
        var items = historyStore.Snapshot().Select(entry => new HistoryViewItem(
            entry.CreatedAt.ToLocalTime().ToString("MM-dd  HH:mm"), entry.Text)).ToArray();
        HistoryList.ItemsSource = items;
        EmptyHistoryText.Visibility = items.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
    }
    private void CopyHistory_Click(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string text } && !string.IsNullOrWhiteSpace(text))
            StatusText.Text = ClipboardService.TrySetText(text, out _) ? "已复制" : "复制未完成，请重试。";
    }
    private void ClearHistory_Click(object sender, RoutedEventArgs e) => historyStore.Clear();

    private void Exit_Click(object sender, RoutedEventArgs e) => exitApplication();
    public sealed record HistoryViewItem(string DisplayTime, string Text);

#if DEBUG
    // Render our own XAML for layout review; never captures the desktop or injects input.
    private async Task CapturePreviewsAsync()
    {
        var directory = Environment.GetEnvironmentVariable("VOICEINPUT_UI_PREVIEW_DIRECTORY");
        if (string.IsNullOrWhiteSpace(directory)) return;
        previewing = true;
        try
        {
            Directory.CreateDirectory(directory);
            await Task.Delay(1200);
            var folder = await Windows.Storage.StorageFolder.GetFolderFromPathAsync(Path.GetFullPath(directory));
            var showPairing=Environment.GetEnvironmentVariable("VOICEINPUT_UI_PREVIEW_PAIRING")=="1";
            if(showPairing) {
                PairingEditor.Visibility=Visibility.Visible;
                PairingRobotsBox.ItemsSource=new[]{new PairableRobot("000000000000","ESP-SparkBot",System.Net.IPAddress.Loopback,45834)};
                PairingRobotsBox.SelectedIndex=0;PairingCodeBox.Text="123456";
                PairingStatus.Text="配对界面预览";
                PairRobotButton.IsEnabled=PairingSearchButton.IsEnabled=CancelPairingButton.IsEnabled=false;
            }
            foreach (var dark in new[] { false, true })
            {
                loaded = false; DarkModeToggle.IsOn = dark; loaded = true;
                Root.RequestedTheme = dark ? ElementTheme.Dark : ElementTheme.Light;
                foreach (var page in new[] { 0, 2 })
                {
                    Navigation.SelectedIndex = page;
                    Root.UpdateLayout();
                    await Task.Delay(700);
                    var bitmap = new Microsoft.UI.Xaml.Media.Imaging.RenderTargetBitmap();
                    await bitmap.RenderAsync(Root);
                    var buffer = await bitmap.GetPixelsAsync();
                    var bytes = System.Runtime.InteropServices.WindowsRuntime.WindowsRuntimeBufferExtensions.ToArray(buffer);
                    // RenderTargetBitmap omits the system backdrop. Composite onto a matching neutral surface.
                    var surface = dark ? (byte)28 : (byte)245;
                    for (var i = 0; i < bytes.Length; i += 4)
                    {
                        var alpha = bytes[i + 3];
                        for (var c = 0; c < 3; c++)
                            bytes[i + c] = (byte)Math.Min(255, bytes[i + c] + surface * (255 - alpha) / 255);
                        bytes[i + 3] = 255;
                    }
                    var name = "settings-" + (dark ? "dark-" : "light-") + (page == 0 ? "robot" : "general") + ".png";
                    var file = await folder.CreateFileAsync(name, Windows.Storage.CreationCollisionOption.ReplaceExisting);
                    using var stream = await file.OpenAsync(Windows.Storage.FileAccessMode.ReadWrite);
                    var encoder = await Windows.Graphics.Imaging.BitmapEncoder.CreateAsync(
                        Windows.Graphics.Imaging.BitmapEncoder.PngEncoderId, stream);
                    encoder.SetPixelData(Windows.Graphics.Imaging.BitmapPixelFormat.Bgra8,
                        Windows.Graphics.Imaging.BitmapAlphaMode.Ignore, (uint)bitmap.PixelWidth,
                        (uint)bitmap.PixelHeight, 96, 96, bytes);
                    await encoder.FlushAsync();
                }
            }

            if(showPairing) {PairingEditor.Visibility=Visibility.Collapsed;PairingCodeBox.Text=string.Empty;PairingRobotsBox.ItemsSource=null;PairRobotButton.IsEnabled=PairingSearchButton.IsEnabled=CancelPairingButton.IsEnabled=true;}
            loaded = false; DarkModeToggle.IsOn = config.DarkMode; loaded = true;
            Navigation.SelectedIndex = 0;
            ApplyTheme();
        }
        catch { AppDiagnostics.Info("Settings preview capture failed."); }
        finally { previewing = false; }
    }
#endif

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(nint hwnd);
    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(nint hwnd, int attribute, ref int value, int size);
}
