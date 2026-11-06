using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading.Channels;

namespace VoiceInput.Robot;

/// <summary>Paired, TLS-encrypted LAN transport. Discovery carries no credentials or transcripts.</summary>
public sealed class WifiRobotServer : IAsyncDisposable
{
    public const int DefaultPort = 45831;
    public const int DiscoveryPort = 45832;
    private readonly TcpListener listener;
    private readonly UdpClient? discovery;
    private readonly X509Certificate2 certificate;
    private readonly string token, identity;
    private readonly CancellationTokenSource lifetime = new();
    private readonly Channel<IRobotFeedbackWire> pending = Channel.CreateBounded<IRobotFeedbackWire>(1);
    private readonly Task acceptTask, discoveryTask;
    private readonly System.Collections.Concurrent.ConcurrentDictionary<TcpClient, byte> clients = new();
    private int occupied;
    public int Port => ((IPEndPoint)listener.LocalEndpoint).Port;

    public WifiRobotServer(X509Certificate2 certificate, string token, string identity,
        IPAddress? address = null, int port = DefaultPort, bool enableDiscovery = true)
    {
        if (token.Length != 64 || !token.All(Uri.IsHexDigit) || identity.Length != 32 || !identity.All(Uri.IsHexDigit))
            throw new ArgumentException("Invalid pairing identity");
        this.certificate = certificate;
        this.token = token;
        this.identity = identity;
        listener = new TcpListener(address ?? IPAddress.Any, port);
        listener.Start(4);
        try { discovery = enableDiscovery ? new UdpClient(new IPEndPoint(IPAddress.Any, DiscoveryPort)) : null; }
        catch { listener.Stop(); throw; }
        acceptTask = AcceptLoop();
        discoveryTask = discovery is null ? Task.CompletedTask : DiscoverLoop();
    }

    public IRobotFeedbackWire? AcceptWire()
    {
        if (pending.Reader.TryRead(out var value)) return value;
        lifetime.Token.WaitHandle.WaitOne(100);
        return pending.Reader.TryRead(out value) ? value : null;
    }

    private async Task AcceptLoop()
    {
        while (!lifetime.IsCancellationRequested)
        {
            try
            {
                var client = await listener.AcceptTcpClientAsync(lifetime.Token);
                if (clients.Count >= 4) { client.Dispose(); continue; }
                clients.TryAdd(client, 0);
                _ = Authenticate(client);
            }
            catch (OperationCanceledException) { break; }
            catch (SocketException) when (lifetime.IsCancellationRequested) { break; }
            catch { await Task.Delay(200, lifetime.Token).ConfigureAwait(false); }
        }
    }

    private async Task Authenticate(TcpClient client)
    {
        var claimed = false;
        SslStream? tls = null;
        try
        {
            client.NoDelay = true;
            tls = new SslStream(client.GetStream(), false);
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
            timeout.CancelAfter(TimeSpan.FromSeconds(8));
            await tls.AuthenticateAsServerAsync(new SslServerAuthenticationOptions
            {
                ServerCertificate = certificate, ClientCertificateRequired = false,
                EnabledSslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13,
                CertificateRevocationCheckMode = X509RevocationMode.NoCheck
            }, timeout.Token);
            var auth = await ReadBoundedLine(tls, 100, timeout.Token);
            var expected = Encoding.ASCII.GetBytes("@CVI-AUTH1 " + token);
            if (!CryptographicOperations.FixedTimeEquals(Encoding.ASCII.GetBytes(auth), expected)) return;
            if (Interlocked.CompareExchange(ref occupied, 1, 0) != 0) return;
            claimed = true;
            await tls.WriteAsync(Encoding.ASCII.GetBytes("@CVI-AUTH1 OK\n"), timeout.Token);
            var ownedClient = client;
            var wire = new TlsWire(client, tls, () => { Interlocked.Exchange(ref occupied, 0); clients.TryRemove(ownedClient, out _); });
            tls = null;
            if (!pending.Writer.TryWrite(wire)) wire.Dispose();
            else { client = null!; claimed = false; }
        }
        catch (Exception error) {
            if (Environment.GetEnvironmentVariable("VOICEINPUT_WIFI_DIAGNOSTICS")=="1")
                Console.Error.WriteLine("Wi-Fi authentication: "+error.GetType().Name+": "+error.Message);
        }
        finally
        {
            tls?.Dispose();
            if (client is not null) { clients.TryRemove(client, out _); client.Dispose(); }
            if (claimed) Interlocked.Exchange(ref occupied, 0);
        }
    }

    public static async Task<string> ReadBoundedLine(Stream stream, int maximum, CancellationToken token)
    {
        var bytes = new List<byte>();
        var one = new byte[1];
        while (await stream.ReadAsync(one, token) != 0)
        {
            if (one[0] == 10) return Encoding.UTF8.GetString(bytes.ToArray()).TrimEnd('\r');
            if (bytes.Count >= maximum) throw new IOException("frame_too_large");
            bytes.Add(one[0]);
        }
        throw new IOException("peer_closed");
    }

    private async Task DiscoverLoop()
    {
        var expected = "CVI-DISCOVER/1 " + identity;
        var reply = Encoding.ASCII.GetBytes("CVI-HOST/1 " + identity + " " + Port);
        while (!lifetime.IsCancellationRequested)
        {
            try
            {
                var request = await discovery!.ReceiveAsync(lifetime.Token);
                if (request.Buffer.Length < 100 && Encoding.ASCII.GetString(request.Buffer) == expected)
                    await discovery.SendAsync(reply, request.RemoteEndPoint, lifetime.Token);
            }
            catch (OperationCanceledException) { break; }
            catch (ObjectDisposedException) { break; }
            catch { if (!lifetime.IsCancellationRequested) await Task.Delay(200, lifetime.Token); }
        }
    }

    private sealed class TlsWire : IRobotFeedbackWire
    {
        private readonly TcpClient client;
        private readonly SslStream tls;
        private readonly Action released;
        private readonly CancellationTokenSource lifetime = new();
        private readonly Channel<string> lines = Channel.CreateBounded<string>(32);
        private readonly Task reader;
        private int disposed;
        public TlsWire(TcpClient client, SslStream tls, Action released)
        { this.client = client; this.tls = tls; this.released = released; reader = Read(); }
        private async Task Read()
        {
            try
            {
                while (!lifetime.IsCancellationRequested)
                {
                    var line = await ReadBoundedLine(tls, 20000, lifetime.Token);
                    if (!lines.Writer.TryWrite(line)) throw new IOException("queue_full");
                }
            }
            catch { }
            finally { lines.Writer.TryComplete(); }
        }
        public string? ReadLine()
        {
            if (lines.Reader.TryRead(out var line)) return line;
            if (lines.Reader.Completion.IsCompleted) throw new IOException("wifi_disconnected");
            lifetime.Token.WaitHandle.WaitOne(50);
            return lines.Reader.TryRead(out line) ? line : null;
        }
        public void WriteLine(string value)
        {
            if (disposed != 0) throw new IOException("wifi_disconnected");
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            tls.WriteAsync(Encoding.UTF8.GetBytes(value + "\n"), timeout.Token).AsTask().GetAwaiter().GetResult();
        }
        public void Dispose()
        {
            if (Interlocked.Exchange(ref disposed, 1) != 0) return;
            lifetime.Cancel(); client.Dispose(); tls.Dispose(); released();
        }
    }

    public async ValueTask DisposeAsync()
    {
        lifetime.Cancel(); listener.Stop(); discovery?.Dispose();
        foreach (var client in clients.Keys) client.Dispose();
        while (pending.Reader.TryRead(out var wire)) wire.Dispose();
        try { await Task.WhenAll(acceptTask, discoveryTask); } catch (OperationCanceledException) { }
        lifetime.Dispose();
    }
}
