using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using VoiceInput.Robot;

internal static class WifiContract
{
    private static void Check(bool ok,string reason) {if(!ok)throw new InvalidOperationException(reason);}
    public static async Task<int> RunAll()
    {
        using var rsa=RSA.Create(2048);
        var request=new CertificateRequest("CN=test",rsa,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);
        using var generated=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1),DateTimeOffset.UtcNow.AddDays(1));
        using var certificate=new X509Certificate2(generated.Export(X509ContentType.Pfx,""),"",X509KeyStorageFlags.UserKeySet);
        var secret=new string('a',64);
        await using var server=new WifiRobotServer(certificate,secret,new string('b',32),IPAddress.Loopback,0,false);
        using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(20));
        async Task<(TcpClient,SslStream)> Client(string token,bool wrongCertificate=false) {
            var tcp=new TcpClient();
            try {
                await tcp.ConnectAsync(IPAddress.Loopback,server.Port,timeout.Token);
                var tls=new SslStream(tcp.GetStream(),false,(_,peer,_,_)=>!wrongCertificate&&peer?.GetCertHashString()==certificate.GetCertHashString());
                await tls.AuthenticateAsClientAsync(new SslClientAuthenticationOptions{TargetHost="test",EnabledSslProtocols=SslProtocols.Tls12},timeout.Token);
                await tls.WriteAsync(Encoding.ASCII.GetBytes("@CVI-AUTH1 "+token+"\n"),timeout.Token);
                return (tcp,tls);
            } catch {tcp.Dispose();throw;}
        }
        async Task<IRobotFeedbackWire> Wire() {
            while(true) {var value=server.AcceptWire();if(value is not null)return value;await Task.Delay(20,timeout.Token);}
        }
        var denied=await Client(new string('c',64));
        using(denied.Item1) using(denied.Item2) {
            var failed=false;
            try {await WifiRobotServer.ReadBoundedLine(denied.Item2,100,timeout.Token);}catch(IOException){failed=true;}
            Check(failed,"wrong token must be rejected");
        }
        Check(server.AcceptWire() is null,"unauthenticated client must never reach input protocol");
        Console.WriteLine("PASS Wi-Fi wrong pairing token rejected before protocol");
        var rejected=false;
        try {var bad=await Client(secret,true);bad.Item1.Dispose();bad.Item2.Dispose();}catch(Exception error) when(error is AuthenticationException or IOException){rejected=true;}
        Check(rejected,"wrong pinned certificate");
        Console.WriteLine("PASS Wi-Fi certificate mismatch rejected");
        var valid=await Client(secret);
        using(valid.Item1) using(valid.Item2) using(var wire=await Wire()) {
            Check(await WifiRobotServer.ReadBoundedLine(valid.Item2,100,timeout.Token)=="@CVI-AUTH1 OK","auth ack");
            var bytes=Encoding.UTF8.GetBytes("@CVI1 测试原文\n");
            foreach(var value in bytes) await valid.Item2.WriteAsync(new byte[]{value},timeout.Token);
            string? line=null;while(line is null){line=wire.ReadLine();await Task.Delay(5,timeout.Token);}
            Check(line=="@CVI1 测试原文","fragmented unicode preserved");
            wire.WriteLine("@CVI1 reply");
            Check(await WifiRobotServer.ReadBoundedLine(valid.Item2,100,timeout.Token)=="@CVI1 reply","duplex");
            var second=await Client(secret);
            using(second.Item1) using(second.Item2) {
                var closed=false;try{await WifiRobotServer.ReadBoundedLine(second.Item2,100,timeout.Token);}catch(IOException){closed=true;}
                Check(closed,"second connection must not take over");
            }
        }
        Console.WriteLine("PASS Wi-Fi authenticated Unicode duplex and no second-client takeover");
        var resumed=await Client(secret);
        using(resumed.Item1) using(resumed.Item2) using(var wire=await Wire()) {
            Check(await WifiRobotServer.ReadBoundedLine(resumed.Item2,100,timeout.Token)=="@CVI-AUTH1 OK","reconnect");
            Check(wire.ReadLine() is null,"no old frame replayed");
            await resumed.Item2.WriteAsync(Encoding.ASCII.GetBytes(new string('x',20001)+"\n"),timeout.Token);
            var closed=false;
            while(!closed) {try{wire.ReadLine();await Task.Delay(10,timeout.Token);}catch(IOException){closed=true;}}
        }
        Console.WriteLine("PASS Wi-Fi reconnect discards old frames and bounds incoming data");
        return 4;
    }
}
